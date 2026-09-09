$ErrorActionPreference='Stop'
$testRoot=Join-Path ([IO.Path]::GetTempPath()) ('castweave-test-'+[guid]::NewGuid().ToString('N'))
$target=Join-Path $testRoot 'installed/castweave'
New-Item -ItemType Directory -Path (Join-Path $target 'bin/64bit') -Force | Out-Null
Set-Content -LiteralPath (Join-Path $target 'bin/64bit/castweave.dll') -Value 'old'
$source=Join-Path $testRoot 'payload/castweave'
New-Item -ItemType Directory -Path (Join-Path $source 'bin/64bit') -Force | Out-Null
Set-Content -LiteralPath (Join-Path $source 'bin/64bit/castweave.dll') -Value 'new'
$zip=Join-Path $testRoot 'good.zip'
Compress-Archive -LiteralPath $source -DestinationPath $zip
$hash=(Get-FileHash -LiteralPath $zip).Hash
$helper=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../data/install-update.ps1'))
$log=Join-Path $testRoot 'test.log'
& powershell.exe -NoProfile -File $helper -Zip $zip -Destination $target -ExpectedHash $hash -LogPath $log -ValidateOnly
if($LASTEXITCODE -ne 0) { throw 'Valid archive was rejected' }
& powershell.exe -NoProfile -File $helper -Zip $zip -Destination $target -ExpectedHash ('0'*64) -LogPath $log -ValidateOnly
if($LASTEXITCODE -eq 0) { throw 'Invalid checksum accepted' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$bad=Join-Path $testRoot 'bad.zip'
$archive=[IO.Compression.ZipFile]::Open($bad,[IO.Compression.ZipArchiveMode]::Create)
[void]$archive.CreateEntry('castweave/../../escape.txt')
$archive.Dispose()
& powershell.exe -NoProfile -File $helper -Zip $bad -Destination $target -ExpectedHash (Get-FileHash $bad).Hash -LogPath $log -ValidateOnly
if($LASTEXITCODE -eq 0) { throw 'Unsafe ZIP accepted' }
# Simulate OBS being closed only inside this test process. The target is a new temporary fixture.
$testState=@{checks=0;injected=$false}
function Get-Process { param($Name) $testState.checks++; if($testState.checks -eq 1) { return [pscustomobject]@{Name='simulated OBS'} }; return $null }
& $helper -Zip $zip -Destination $target -ExpectedHash $hash -LogPath $log -Quiet -WaitSeconds 5
if((Get-Content -LiteralPath (Join-Path $target 'bin/64bit/castweave.dll') -Raw).Trim() -ne 'new') { throw 'New DLL was not copied' }
$backup=Get-ChildItem -LiteralPath $testRoot -Directory -Filter 'backup-*' | Select-Object -First 1
if(!$backup -or (Get-Content -LiteralPath (Join-Path $backup.FullName 'bin/64bit/castweave.dll') -Raw).Trim() -ne 'old') { throw 'Backup did not preserve old DLL' }
if($testState.checks -lt 3) { throw 'Installer did not wait for OBS' }
Set-Content -LiteralPath (Join-Path $target 'bin/64bit/castweave.dll') -Value 'rollback-original'

function Copy-Item {
 param($LiteralPath,$Destination,[switch]$Recurse,[switch]$Force)
 Microsoft.PowerShell.Management\Copy-Item -LiteralPath $LiteralPath -Destination $Destination -Recurse:$Recurse -Force:$Force
 if(!$testState.injected -and $Destination -eq $target -and $LiteralPath -like '*unpack-*') {
   $testState.injected=$true
   throw 'Simulated copy failure after replacing files'
 }
}
& $helper -Zip $zip -Destination $target -ExpectedHash $hash -LogPath $log -Quiet -WaitSeconds 5
if(!$testState.injected -or (Get-Content -LiteralPath (Join-Path $target 'bin/64bit/castweave.dll') -Raw).Trim() -ne 'rollback-original') { throw 'Rollback failed' }

# Exercise restart handoff entirely in a temporary fixture. Never close or launch real OBS.
$fakeObs=Join-Path $testRoot 'obs64.exe'
Set-Content -LiteralPath $fakeObs -Value 'test executable placeholder'
$stateFile=Join-Path $testRoot 'restart-state.json'
$resultFile=Join-Path $testRoot 'last-update.json'
$testState.checks=0; $testState.reopened=$false; $testState.cancel=$false; $testState.reopenedEarly=$false
function Get-Process {
 param($Name)
 $testState.checks++
 if($testState.checks -le 2) {
  if($testState.checks -eq 2) {
   if((Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json).phase -ne 'ready') { throw 'Parent exit was requested before readiness' }
   if($testState.reopenedEarly) { return [pscustomobject]@{Id=($PID+1);Name='new simulated OBS'} }
   if($testState.cancel) { Set-Content -LiteralPath ($stateFile+'.cancel') -Value 'cancel' }
  }
  return [pscustomobject]@{Id=$PID;Name='simulated OBS'}
 }
 return $null
}
function Start-Process {
 param($FilePath,$WorkingDirectory,$ArgumentList)
 if($FilePath -ne $fakeObs -or $WorkingDirectory -ne $testRoot -or $ArgumentList -ne '--portable') { throw 'Wrong restart executable or arguments' }
 if((Get-Content -LiteralPath (Join-Path $target 'bin/64bit/castweave.dll') -Raw).Trim() -ne 'new') { throw 'OBS restarted before installation' }
 if((Get-Content -LiteralPath $resultFile -Raw | ConvertFrom-Json).phase -ne 'installed') { throw 'Installation success not persisted' }
 $testState.reopened=$true
}
& $helper -Zip $zip -Destination $target -ExpectedHash $hash -LogPath $log -Quiet -WaitSeconds 5 -RestartExecutable $fakeObs -ParentProcessId $PID -StatePath $stateFile -ResultPath $resultFile -Portable
if(!$testState.reopened) { throw 'OBS relaunch was not requested' }
Set-Content -LiteralPath (Join-Path $target 'bin/64bit/castweave.dll') -Value 'cancel-original'
$testState.checks=0; $testState.reopened=$false; $testState.cancel=$true
& $helper -Zip $zip -Destination $target -ExpectedHash $hash -LogPath $log -Quiet -WaitSeconds 5 -RestartExecutable $fakeObs -ParentProcessId $PID -StatePath $stateFile -ResultPath $resultFile
if($testState.reopened -or (Get-Content -LiteralPath (Join-Path $target 'bin/64bit/castweave.dll') -Raw).Trim() -ne 'cancel-original') { throw 'Cancelled restart changed the installation' }
if((Get-Content -LiteralPath $resultFile -Raw | ConvertFrom-Json).phase -ne 'cancelled') { throw 'Cancellation was not persisted' }
$testState.checks=0; $testState.reopened=$false; $testState.cancel=$false; $testState.reopenedEarly=$true
$stateFile=Join-Path $testRoot 'reopened-state.json'
& $helper -Zip $zip -Destination $target -ExpectedHash $hash -LogPath $log -Quiet -WaitSeconds 5 -RestartExecutable $fakeObs -ParentProcessId $PID -StatePath $stateFile -ResultPath $resultFile
if($testState.reopened -or (Get-Content -LiteralPath (Join-Path $target 'bin/64bit/castweave.dll') -Raw).Trim() -ne 'cancel-original') { throw 'Early reopening changed the installation' }
$result=Get-Content -LiteralPath $resultFile -Raw | ConvertFrom-Json
if($result.phase -ne 'failed' -or $result.message -notlike 'OBS was opened again*') { throw 'Early reopening was not reported' }
'PASS: archive safety, backup, rollback, relaunch, cancellation and early-reopen detection.'
exit 0
