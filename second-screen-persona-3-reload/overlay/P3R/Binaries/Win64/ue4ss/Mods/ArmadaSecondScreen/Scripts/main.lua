-- Armada second-screen provider for Persona 3 Reload (UE4SS Lua). No sockets here, so it
-- uses the file link. Most state sits in memory the engine does not describe and is read
-- as raw bytes through the stand-in UE4SS.dll (peek.req out, peek.bin back).
local dir = os.getenv("ARMADA_MOD_LINK")
if not dir then print("[ArmadaSecondScreen] no link dir, disabled\n") return end
local base = "Z:" .. dir:gsub("/", "\\") .. "\\"
local dev = os.getenv("ARMADA_MOD_DEV")

local function esc(s)
  return (tostring(s):gsub('[%c"\\]', function(c)
    if c == '"' then return '\\"' elseif c == "\\" then return "\\\\" else return string.format("\\u%04x", c:byte()) end
  end))
end
local function json(v)
  local t = type(v)
  if t == "table" then
    local parts = {}
    if #v > 0 or next(v) == nil then
      for i = 1, #v do parts[i] = json(v[i]) end
      return "[" .. table.concat(parts, ",") .. "]"
    end
    for k, x in pairs(v) do parts[#parts + 1] = '"' .. esc(k) .. '":' .. json(x) end
    return "{" .. table.concat(parts, ",") .. "}"
  elseif t == "number" then
    if v ~= v or v == math.huge or v == -math.huge then return "0" end
    return math.type(v) == "integer" and tostring(v) or string.format("%.14g", v)
  elseif t == "boolean" then return tostring(v)
  elseif t == "nil" then return "null" end
  return '"' .. esc(v) .. '"'
end

local function write(name, text)
  local f = io.open(base .. name .. ".tmp", "wb")
  if not f then return end
  f:write(text) f:close()
  os.remove(base .. name)
  os.rename(base .. name .. ".tmp", base .. name)
end

local function send_request(reqs)
  os.remove(base .. "peek.bin")
  local lines = {}
  for i, r in ipairs(reqs) do lines[i] = string.format("%x %x\n", r[1], r[2]) end
  write("peek.req", table.concat(lines))
end
local function read_reply(reqs)
  local f = io.open(base .. "peek.bin", "rb")
  if not f then return nil end
  local data = f:read("a") f:close()
  local out, at = {}, 1
  for i, r in ipairs(reqs) do out[i] = data:sub(at, at + r[2] - 1) at = at + r[2] end
  if at - 1 ~= #data then return nil end
  return out
end

-- Asks for {address, length} pairs and calls done with one string per pair, or
-- nil when no answer came. The answer is picked up on a later tick, so the game
-- thread never waits. UE4SS calls fail inside a Lua coroutine, hence callbacks.
local pending
local function peek(reqs, done)
  send_request(reqs)
  pending = { reqs = reqs, done = done, ticks = 0 }
end
local function poll()
  if not pending then return false end
  local p = pending
  local r = read_reply(p.reqs)
  p.ticks = p.ticks + 1
  if r or p.ticks > 20 then pending = nil p.done(r) end
  return true
end

-- Blocking variant for the development channel only.
function ArmadaPeek(reqs)
  send_request(reqs)
  local t0 = os.clock()
  while os.clock() - t0 < 1 do
    local r = read_reply(reqs)
    if r then return r end
  end
  return nil
end

local function utf8_from_utf16(s)
  local out = {}
  for i = 1, #s - 1, 2 do
    local c = string.unpack("<I2", s, i)
    if c == 0 then break end
    out[#out + 1] = (c >= 0xD800 and c < 0xE000) and "?" or utf8.char(c)
  end
  return table.concat(out)
end

-- A name table is an array of engine text values (0x18 bytes each). The shown
-- string of each is three pointers away: value -> text data -> string -> characters.
local function read_names(path, done)
  local asset = StaticFindObject(path)
  if not asset or not asset:IsValid() then return done(nil) end
  peek({ { asset:GetAddress() + 0x30, 12 } }, function(head)
    if not head then return done(nil) end
    local ptr, num = string.unpack("<I8I4", head[1])
    if ptr == 0 or num == 0 or num > 4096 then return done(nil) end
    peek({ { ptr, num * 0x18 } }, function(rows)
      if not rows then return done(nil) end
      local reqs, index = {}, {}
      for i = 0, num - 1 do
        local td = string.unpack("<I8", rows[1], i * 0x18 + 1)
        if td ~= 0 then reqs[#reqs + 1] = { td + 8, 8 } index[#index + 1] = i end
      end
      peek(reqs, function(strs)
        if not strs then return done(nil) end
        for i, s in ipairs(strs) do reqs[i] = { string.unpack("<I8", s), 12 } end
        peek(reqs, function(heads)
          if not heads then return done(nil) end
          for i, s in ipairs(heads) do
            local data, n = string.unpack("<I8I4", s)
            reqs[i] = { data, math.max(2, math.min(n, 96) * 2) }
          end
          peek(reqs, function(chars)
            if not chars then return done(nil) end
            local names = {}
            for i, s in ipairs(chars) do names[index[i]] = utf8_from_utf16(s) end
            done(names)
          end)
        end)
      end)
    end)
  end)
end

-- Points needed for each social stat rank, from the game's own table.
local function read_thresholds()
  local out = {}
  local g = FindFirstOf("GlobalWork")
  g.pCommunityWork.pHeroParameterHandle.pDataAsset.Tables:ForEach(function(i, t)
    local row, sum = {}, 0
    t:get().Points:ForEach(function(j, p) sum = sum + p:get() row[#row + 1] = sum end)
    out[i] = row
  end)
  return #out == 3 and out or nil
end
local thresholds = { { 0, 20, 55, 100, 155, 230 }, { 0, 15, 30, 45, 70, 100 }, { 0, 15, 30, 45, 60, 80 } }

local function rank_of(points, needs)
  local rank = 1
  for i, need in ipairs(needs) do if points >= need then rank = i end end
  return rank
end

-- Offsets in the game instance (Steam build): party units, active party,
-- money, calendar, social links followed by the three social stats, names.
local UNITS, UNIT_SIZE, ACTIVE, MONEY, CALENDAR, LINKS, NAME = 0x1b0, 0x2b4, 0x24d4, 0x4260, 0xa8e0, 0xc5f0, 0xc6d0

local persona_names, skill_names
local function persona(block, at)
  local flags, id, level = string.unpack("<i2I2I2", block, at)
  if id == 0 or flags == 0 then return nil end
  local p = { id = id, level = level, name = persona_names[id] or ("Persona " .. id), skills = {}, stats = {} }
  for s = 0, 7 do
    local skill = string.unpack("<I2", block, at + 0xc + s * 2)
    if skill ~= 0 then p.skills[#p.skills + 1] = skill_names[skill] or ("Skill " .. skill) end
  end
  for s = 0, 4 do
    p.stats[s + 1] = math.min(99, block:byte(at + 0x1c + s) + block:byte(at + 0x21 + s) + block:byte(at + 0x26 + s))
  end
  return p
end

local function build(a, r)
  local units, active, links = r[1], r[2], r[5]
  local day, time = string.unpack("<I4B", r[4])
  local hero_flags = string.unpack("<I4", units, UNIT_SIZE + 1)
  if hero_flags == 0 then return { ready = false } end
  local s = { ready = true, day = day, time = time, money = string.unpack("<I4", r[3]),
              name = r[6]:match("^[^%z]*"), party = {}, personas = {}, links = {}, stats = {} }
  local ok, age = pcall(function() return StaticFindObject("/Script/xrd777.Default__CldBpFuncLib"):GetNowMoonAge() end)
  if ok and type(age) == "number" then s.moon = age end
  local in_party = {}
  for i = 0, 9 do local id = string.unpack("<I2", active, i * 2 + 1) if id ~= 0 then in_party[id] = i + 1 end end
  for id = 1, 10 do
    local at = id * UNIT_SIZE + 1
    local flags = string.unpack("<I4", units, at)
    -- only the members in the battle party: the others may not have joined yet
    if flags ~= 0 and in_party[id] then
      local hp, sp, _, _, level = string.unpack("<i4i4i4I4i2", units, at + 0xc)
      local equip = string.unpack("<I2", units, at + 0x48)
      local m = { id = id, hp = hp, sp = sp, level = level, slot = in_party[id] or 0 }
      if id == 1 then
        for k = 0, 11 do
          local p = persona(units, at + 0x4c + k * 0x30)
          if p then p.equipped = k == equip s.personas[#s.personas + 1] = p if p.equipped then m.persona = p.name end end
        end
      else
        local p = persona(units, at + 0x4c)
        if p then m.persona = p.name m.level = p.level m.skills = p.skills m.stats = p.stats end
      end
      s.party[#s.party + 1] = m
    end
  end
  for arcana = 1, 22 do
    local rank = string.unpack("<I2", links, arcana * 8 + 1)
    if rank > 0 then s.links[#s.links + 1] = { arcana = arcana, rank = rank } end
  end
  for i = 0, 2 do
    local points = string.unpack("<I4", links, 0xc8 + i * 8 + 4 + 1)
    s.stats[i + 1] = rank_of(points, thresholds[i + 1])
  end
  return s
end

local last, failures, dead, wait = "", 0, false, 0
local work, ticks, at_title, seen_title = nil, 0, true, false
local function publish(s)
  local text = json(s)
  if text ~= last then last = text write("state.json", text) end
end
local function fail(err)
  print("[ArmadaSecondScreen] " .. tostring(err) .. "\n")
  failures = failures + 1
  -- A provider that keeps failing (for example after a game update) lets go of the screen.
  if failures > 40 then dead = true write("state.json", '{"ready":false,"visible":false}') else publish({ ready = false }) end
end

local function step()
  if wait > 0 then wait = wait - 1 return end
  if not persona_names then
    return read_names("/Game/Xrd777/Blueprints/Common/Names/DatPersonaNameDataAsset.DatPersonaNameDataAsset", function(names)
      if not names then wait = 10 return end
      read_names("/Game/Xrd777/Blueprints/Common/Names/DatSkillNameDataAsset.DatSkillNameDataAsset", function(skills)
        if not skills then wait = 10 return end
        persona_names, skill_names = names, skills
        local ok, t = pcall(read_thresholds)
        if ok and t then thresholds = t end
      end)
    end)
  end
  -- The game instance outlives everything; the title actor only exists on the title screen.
  if not work or not work:IsValid() then work = FindFirstOf("GlobalWork") end
  if not work or not work:IsValid() then return publish({ ready = false }) end
  ticks = ticks + 1
  if ticks % 8 == 1 then
    local title = FindFirstOf("BP_TitleActor_C")
    at_title = title ~= nil and title:IsValid()
    seen_title = seen_title or at_title
  end
  -- before the title screen first appears the game instance holds a blank new game
  if at_title or not seen_title then return publish({ ready = false }) end
  local a = work:GetAddress()
  peek({ { a + UNITS, UNIT_SIZE * 11 }, { a + ACTIVE, 20 }, { a + MONEY, 4 }, { a + CALENDAR, 8 },
         { a + LINKS, 0xe0 }, { a + NAME + 0x58, 0x40 } }, function(r)
    if not r then return fail("no answer to a read") end
    local ok, s = pcall(build, a, r)
    if ok then failures = 0 publish(s) else fail(s) end
  end)
end

-- development aid: run Lua dropped in cmd.lua and write the result to reply.txt
local function run_cmd()
  if not dev then return end
  local f = io.open(base .. "cmd.lua", "r")
  if not f then return end
  local src = f:read("a") f:close()
  os.remove(base .. "cmd.lua")
  local fn, err = load(src)
  local out
  if not fn then out = "load error: " .. tostring(err)
  else local ok, r = pcall(fn) out = tostring(r) end
  write("reply.txt", out)
end

write("hello.json", '{"game":"Persona 3 Reload","appid":2161700,"provider":"p3r-companion","version":1}')
-- UObjects are only touched on the game thread. This works with every optional UE4SS hook
-- disabled, which is how it has to run under Proton on ARM.
LoopAsync(150, function()
  ExecuteInGameThread(function()
    pcall(run_cmd)
    if dead then return end
    local ok, err = pcall(function() if not poll() then step() end end)
    if not ok then pending = nil fail(err) end
  end)
  return false
end)
print("[ArmadaSecondScreen] started, link " .. base .. "\n")
