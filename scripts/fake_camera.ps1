param(
  [string]$BackendUrl = "http://localhost:9000",
  [Parameter(Mandatory=$true)]
  [string]$ImagePath
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ImagePath)) {
  throw "ImagePath not found: $ImagePath"
}

# Multipart form upload for /ingest/frame
$boundary = [System.Guid]::NewGuid().ToString()
$LF = "`r`n"
$fileBytes = [System.IO.File]::ReadAllBytes($ImagePath)
$fileName = [System.IO.Path]::GetFileName($ImagePath)

$bodyStream = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.StreamWriter($bodyStream)

$writer.Write("--$boundary$LF")
$writer.Write("Content-Disposition: form-data; name=`"file`"; filename=`"$fileName`"$LF")
$writer.Write("Content-Type: image/jpeg$LF$LF")
$writer.Flush()
$bodyStream.Write($fileBytes, 0, $fileBytes.Length)
$writer.Write("$LF--$boundary--$LF")
$writer.Flush()

$bodyStream.Position = 0
$headers = @{ "Content-Type" = "multipart/form-data; boundary=$boundary" }

Invoke-RestMethod -Method Post -Uri "$BackendUrl/ingest/frame" -Headers $headers -Body $bodyStream.ToArray() | Out-Null

Write-Host "Uploaded frame: $ImagePath"
Write-Host "View stream: http://localhost:5173 (camera panel)"
