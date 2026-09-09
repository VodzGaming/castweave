param([Parameter(Mandatory)][string]$Zip,[Parameter(Mandatory)][string]$Destination,[Parameter(Mandatory)][string]$ExpectedHash,[Parameter(Mandatory)][string]$LogPath,[switch]$ValidateOnly,[switch]$Quiet,[string]$RestartExecutable,[int]$ParentProcessId,[string]$StatePath,[string]$ResultPath,[switch]$Portable,[ValidateRange(1,1800)][int]$WaitSeconds=300)
$ErrorActionPreference='Stop'
$updateMutex=$null; $ownsMutex=$false; $installed=$false
$version=''
if([IO.Path]::GetFileName($Zip) -match '^CastWeave-([0-9]+\.[0-9]+\.[0-9]+)-windows-x64\.zip$') { $version=$Matches[1] }
function Write-State([string]$Phase,[string]$Message) {
    if(!$StatePath) { return }
    $row=@{phase=$Phase;message=$Message;version=$version;time=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Compress
    foreach($path in @($StatePath,$ResultPath)) {
      if(!$path) { continue }
      [IO.File]::WriteAllText($path+'.tmp',$row)
      Move-Item -LiteralPath ($path+'.tmp') -Destination $path -Force
    }
}
function Check-Cancel {
    if($StatePath -and (Test-Path -LiteralPath ($StatePath+'.cancel'))) { throw 'Restart cancelled. The downloaded update is still available.' }
}
function Log-Update([string]$Event,[string]$Message) {
    $row=@{time=[DateTime]::UtcNow.ToString('o');event=$Event;message=$Message}|ConvertTo-Json -Compress
    Add-Content -LiteralPath $LogPath -Value $row
}
function Notify-Update([string]$Message) {
    if(!$Quiet -and !$ValidateOnly -and !$StatePath) {
      Add-Type -AssemblyName System.Windows.Forms
      [void][System.Windows.Forms.MessageBox]::Show($Message,'CastWeave update')
    }
}
try {
    Write-State 'preparing' 'Preparing the update. OBS remains open.'
    $root=[IO.Path]::GetFullPath($Destination).TrimEnd('\','/')
    if([IO.Path]::GetFileName($root) -ne 'castweave' -or !(Test-Path -LiteralPath (Join-Path $root 'bin/64bit/castweave.dll'))) { throw 'Unexpected plugin installation directory.' }
    if($RestartExecutable) {
      if([IO.Path]::GetFileName($RestartExecutable) -ne 'obs64.exe' -or !(Test-Path -LiteralPath $RestartExecutable) -or $ParentProcessId -le 0 -or !$StatePath) { throw 'Invalid OBS restart information.' }
    }
    $check=$root
    while($check) {
      if((Get-Item -LiteralPath $check -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Installation path contains a reparse point.' }
      $check=[IO.Path]::GetDirectoryName($check)
    }
    if((Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash -ne $ExpectedHash) { throw 'Update checksum mismatch.' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive=[IO.Compression.ZipFile]::OpenRead($Zip)
    $temp=Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Zip))) ('unpack-'+[guid]::NewGuid().ToString('N'))
    $prefix=[IO.Path]::GetFullPath($temp)+[IO.Path]::DirectorySeparatorChar
    $bytes=0; $hasDll=$false
    try {
      foreach($entry in $archive.Entries) {
        $name=$entry.FullName.Replace('\','/')
        $resolved=[IO.Path]::GetFullPath((Join-Path $temp $name))
        if(!$name.StartsWith('castweave/') -or !$resolved.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -or $name.Contains(':') -or ($name.Split('/') -contains '..')) { throw 'Unsafe archive path.' }
        foreach($segment in $name.Split('/')) {
          if($segment -and ($segment.TrimEnd(' ','.') -ne $segment -or $segment.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0)) { throw 'Unsafe archive filename.' }
        }
        if((($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000) { throw 'Archive contains a symbolic link.' }
        if($name -eq 'castweave/bin/64bit/castweave.dll') { $hasDll=$true }
        $bytes+=$entry.Length
        if($bytes -gt 300MB) { throw 'Expanded archive is too large.' }
      }
    } finally { $archive.Dispose() }
    if(!$hasDll) { throw 'Archive is missing the plugin DLL.' }
    if($ValidateOnly) { Log-Update 'update_validated' 'Archive and destination validation passed.'; exit 0 }
    $nameHash=[Security.Cryptography.SHA256]::Create()
    try { $mutexKey=[BitConverter]::ToString($nameHash.ComputeHash([Text.Encoding]::UTF8.GetBytes($root.ToLowerInvariant()))).Replace('-','') } finally { $nameHash.Dispose() }
    $updateMutex=New-Object Threading.Mutex($false,('Local\CastWeaveUpdate-'+$mutexKey))
    try { $ownsMutex=$updateMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $ownsMutex=$true }
    if(!$ownsMutex) { throw 'Another CastWeave installer is already running.' }
    Check-Cancel
    Expand-Archive -LiteralPath $Zip -DestinationPath $temp
    $source=Join-Path $temp 'castweave'
    if(!(Test-Path -LiteralPath (Join-Path $source 'bin/64bit/castweave.dll'))) { throw 'Archive is missing the plugin DLL.' }
    if($RestartExecutable) {
      if(Get-Process obs64 -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $ParentProcessId }) { throw 'Close other OBS instances before restarting this one.' }
      Check-Cancel
      Write-State 'ready' 'Update ready. Restarting OBS...'
    }
    $limit=[DateTime]::UtcNow.AddSeconds($WaitSeconds)
    while($true) {
      Check-Cancel
      $running=@(Get-Process obs64 -ErrorAction SilentlyContinue | Where-Object { $null -ne $_ })
      if($RestartExecutable) {
        if($running | Where-Object { $_.Id -ne $ParentProcessId }) {
          throw 'OBS was opened again before the update finished. Installation was cancelled. Close extra OBS instances, then try Restart OBS again and wait for it to reopen automatically.'
        }
        if(!($running | Where-Object { $_.Id -eq $ParentProcessId })) { break }
      } elseif(!$running.Count) { break }
      if([DateTime]::UtcNow -gt $limit) { throw 'OBS did not finish shutting down before the timeout. Check its shutdown log; no plugin files were replaced.' }
      Start-Sleep -Milliseconds 250
    }
    Check-Cancel
    if((Get-FileHash -LiteralPath $Zip -Algorithm SHA256).Hash -ne $ExpectedHash) { throw 'Update checksum changed before installation.' }
    Write-State 'installing' 'Installing CastWeave...'
    if(Get-Process obs64 -ErrorAction SilentlyContinue) { throw 'OBS reopened before installation.' }
    if(Get-ChildItem -LiteralPath $root -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { throw 'Plugin contains a reparse point.' }
    $backup=Join-Path ([IO.Path]::GetDirectoryName($temp)) ('backup-'+[guid]::NewGuid().ToString('N'))
    Copy-Item -LiteralPath $root -Destination $backup -Recurse -Force
    try {
      Get-ChildItem -LiteralPath $source -Force | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $root -Recurse -Force }
      if((Get-FileHash -LiteralPath (Join-Path $root 'bin/64bit/castweave.dll')).Hash -ne (Get-FileHash -LiteralPath (Join-Path $source 'bin/64bit/castweave.dll')).Hash) { throw 'Installed DLL verification failed.' }
    } catch {
      Get-ChildItem -LiteralPath $backup -Force | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $root -Recurse -Force }
      throw
    }
    $installed=$true
    Write-State 'installed' ('CastWeave '+$version+' was installed successfully.')
    Log-Update 'update_installed' 'Update installed successfully. OBS can be reopened.'
    if($RestartExecutable) {
      # OBS is the interactive application the user explicitly asked to reopen.
      $launch=@{FilePath=$RestartExecutable;WorkingDirectory=[IO.Path]::GetDirectoryName($RestartExecutable)}
      if($Portable) { $launch.ArgumentList='--portable' }
      Start-Process @launch
      Log-Update 'update_obs_restarted' 'OBS was reopened after installation.'
    } else { Notify-Update 'CastWeave was updated successfully. You can now reopen OBS.' }
} catch {
    $message=$_.Exception.Message
    $phase=if($installed){'restart_failed'}elseif($StatePath -and (Test-Path -LiteralPath ($StatePath+'.cancel'))){'cancelled'}else{'failed'}
    if($installed) { $message='The update installed, but OBS could not reopen automatically. Please open OBS. '+$message }
    Write-State $phase $message
    Log-Update 'update_install_failed' $_.Exception.Message
    Notify-Update ('The update could not be installed. '+$_.Exception.Message+' See the CastWeave diagnostic log for details.')
    exit 1
} finally {
    if($ownsMutex) { $updateMutex.ReleaseMutex() }
    if($updateMutex) { $updateMutex.Dispose() }
}
