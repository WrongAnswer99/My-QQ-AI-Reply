# start_bot.ps1
# 一键启动 NapCat + QQ-AIreply 机器人
# 用法：powershell -ExecutionPolicy Bypass -File .\start_bot.ps1

$Root = $PSScriptRoot
$NapCatDir = Join-Path $Root 'NapCat.Shell'

# ===== 配置（从 config.json 读取，保持单一维护点）=====
$configData = Get-Content (Join-Path $Root 'config.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$QQPath = $configData.qq_path
$BotQQ  = $configData.bot_qq
if (-not $QQPath) {
    Write-Host "[!] ERROR: 无法从 config.json 读取 qq_path" -ForegroundColor Red
    pause; exit 1
}
if (-not $BotQQ) {
    Write-Host "[!] ERROR: 无法从 config.json 读取 bot_qq" -ForegroundColor Red
    pause; exit 1
}
# ==============================

Write-Host "[*] Cleaning up old processes..." -ForegroundColor Yellow
Get-Process -Name 'qq_ai_reply', 'NapCatWinBootMain' -ErrorAction SilentlyContinue | Stop-Process -Force

# 1. 检查路径
if (-not (Test-Path $QQPath)) {
    Write-Host "[!] ERROR: QQ not found at $QQPath" -ForegroundColor Red
    pause; exit 1
}

$exePath = Join-Path $NapCatDir 'NapCatWinBootMain.exe'
$hookDll = Join-Path $NapCatDir 'NapCatWinBootHook.dll'
$napcatMjs = Join-Path $NapCatDir 'napcat.mjs'

if (-not (Test-Path $exePath)) {
    Write-Host "[!] ERROR: NapCat.Shell folder not found or incomplete!" -ForegroundColor Red
    Write-Host "    Please download NapCat from GitHub and extract it to 'NapCat.Shell' folder." -ForegroundColor Red
    pause; exit 1
}

# 2. 动态生成 loadNapCat.js (替代 bat 中的 echo 逻辑)
$loadJsPath = Join-Path $NapCatDir 'loadNapCat.js'
$napcatMjsUrl = ($napcatMjs -replace '\\', '/')
$jsContent = "(async () => {await import(`"file:///$napcatMjsUrl`")})()"
# 使用 NoNewline 避免生成多余的空行
Set-Content -Path $loadJsPath -Value $jsContent -Encoding UTF8 -NoNewline

# 3. 设置环境变量 (替代 bat 中的 set NAPCAT_XXX=...)
$env:NAPCAT_PATCH_PACKAGE = Join-Path $NapCatDir 'qqnt.json'
$env:NAPCAT_LOAD_PATH     = $loadJsPath
$env:NAPCAT_INJECT_PATH   = $hookDll
$env:NAPCAT_LAUNCHER_PATH = $exePath
$env:NAPCAT_MAIN_PATH     = $napcatMjs

# 4. 启动 NapCat (加入 chcp 65001 解决乱码)
Write-Host "[*] Starting NapCat (BotQQ: $BotQQ)..." -ForegroundColor Cyan
$exeArgs = "`"$QQPath`" `"$hookDll`" $BotQQ"
# 关键修改：在 cmd 参数中加入 chcp 65001 >nul &&
$cmdNapCatArgs = "/c `"chcp 65001 >nul && `"$exePath`" $exeArgs & pause`""
Start-Process cmd -ArgumentList $cmdNapCatArgs -WorkingDirectory $NapCatDir

# 5. 启动机器人 (同样加入 chcp 65001 防止你的机器人输出乱码)
$botExe = Join-Path $Root 'build\qq_ai_reply.exe'
if (Test-Path $botExe) {
    Write-Host "[*] Starting bot..." -ForegroundColor Cyan
    $cmdBotArgs = "/c `"chcp 65001 >nul && `"$botExe`" & pause`""
    Start-Process cmd -ArgumentList $cmdBotArgs -WorkingDirectory $Root
} else {
    Write-Host "[!] WARNING: Bot exe not found at $botExe" -ForegroundColor Yellow
}

Write-Host "`n[+] All started! You can close this launcher window." -ForegroundColor Green
