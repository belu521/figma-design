-- ColorFeedback.lua
-- grandMA3 onPC plugin: 实时把 executor 外观颜色 (Appearance) 通过 OSC 发送给 ESP32-S3 控制器。
-- 控制器固件 (firmware/esp32_s3) 在 OSC_LISTEN_PORT (默认 8000) 监听:
--   /btn/<note>   iii  r g b   (0-255)  -> 矩阵按钮 LED 颜色
--   /fader/<cc>   iii  r g b   (0-255)  -> 推子 LED 颜色
-- 颜色为全 0 时控制器恢复默认 LED 行为。
--
-- 使用前提:
--   1. MA3 onPC: Menu -> In & Out -> OSC, 新建一行:
--      Destination IP = ESP32 的 IP, Port = 8000, Mode = UDP, 启用该行。
--      记下该行的编号 (oscConfigSlot)。
--   2. 修改下方 CONFIG 中的 executor 映射, 与 docs/MIDI_MAPPING.md 保持一致。
--   3. 导入插件并运行 (见 docs/OSC_COLOR_FEEDBACK.md)。

local CONFIG = {
    -- MA3 中 Menu -> In & Out -> OSC 配置行编号
    oscConfigSlot = 1,

    -- 轮询间隔 (秒)。0.1 = 每秒 10 次, 颜色变化近乎实时。
    pollInterval = 0.1,

    -- 矩阵按钮: MIDI Note -> executor 编号 (当前页)
    buttonExecutors = {
        [70] = 101, [71] = 102, [72] = 103, [73] = 104, [74] = 105,
        [75] = 106, [76] = 107, [77] = 108, [78] = 109, [79] = 110,
        [80] = 111, [81] = 112, [82] = 113, [83] = 114, [84] = 115,
        [85] = 116, [86] = 117, [87] = 118, [88] = 119, [89] = 120,
    },

    -- 推子: MIDI CC -> executor 编号 (当前页)
    faderExecutors = {
        [46] = 201, [47] = 202, [48] = 203, [49] = 204, [50] = 205,
        [51] = 206, [52] = 207, [53] = 208, [54] = 209, [55] = 210,
    },
}

local lastSentColors = {}
local running = false

-- 尝试多个属性名以兼容不同 MA3 版本, 返回 0-255 RGB。
local function readAppearanceColor(appearance)
    if not appearance then
        return nil
    end

    local function getNumber(names)
        for _, name in ipairs(names) do
            local ok, value = pcall(function()
                return appearance[name]
            end)
            if ok and value ~= nil then
                local num = tonumber(value)
                if num ~= nil then
                    return num
                end
            end
        end
        return nil
    end

    local r = getNumber({ "BackR", "BACKR", "backr" })
    local g = getNumber({ "BackG", "BACKG", "backg" })
    local b = getNumber({ "BackB", "BACKB", "backb" })

    if r == nil or g == nil or b == nil then
        return nil
    end

    -- 属性可能是 0-1 浮点或 0-255 整数, 统一到 0-255。
    if r <= 1 and g <= 1 and b <= 1 then
        r, g, b = r * 255, g * 255, b * 255
    end

    return math.floor(r + 0.5), math.floor(g + 0.5), math.floor(b + 0.5)
end

-- 取得 executor 上对象 (sequence 等) 的外观颜色。
local function getExecutorColor(execNumber)
    local ok, exec = pcall(GetExecutor, execNumber)
    if not ok or not exec then
        return 0, 0, 0
    end

    local object = exec.Object
    if not object then
        return 0, 0, 0
    end

    local appearance = object.Appearance or object.APPEARANCE
    local r, g, b = readAppearanceColor(appearance)
    if r == nil then
        return 0, 0, 0
    end

    return r, g, b
end

local function sendColor(addressPrefix, index, r, g, b)
    local key = addressPrefix .. index
    local packed = r * 65536 + g * 256 + b
    if lastSentColors[key] == packed then
        return
    end
    lastSentColors[key] = packed

    Cmd(string.format(
        'SendOSC %d "/%s/%d,iii,%d,%d,%d"',
        CONFIG.oscConfigSlot, addressPrefix, index, r, g, b
    ))
end

local function pollOnce()
    for note, execNumber in pairs(CONFIG.buttonExecutors) do
        local r, g, b = getExecutorColor(execNumber)
        sendColor("btn", note, r, g, b)
    end
    for cc, execNumber in pairs(CONFIG.faderExecutors) do
        local r, g, b = getExecutorColor(execNumber)
        sendColor("fader", cc, r, g, b)
    end
end

local function loop()
    running = true
    Printf("ColorFeedback: started (interval %.2fs)", CONFIG.pollInterval)

    while running do
        local ok, err = pcall(pollOnce)
        if not ok then
            Printf("ColorFeedback: poll error: %s", tostring(err))
        end
        coroutine.yield(CONFIG.pollInterval)
    end

    Printf("ColorFeedback: stopped")
end

-- 再次运行插件可停止轮询。
local function main()
    if running then
        running = false
        return
    end
    lastSentColors = {}
    loop()
end

return main
