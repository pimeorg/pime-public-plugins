-- UTF-8 helpers for pime Lua plugins.
-- Text indices in the pime data model count Unicode scalar values, not bytes.

local M = {}

local function decode_at(s, i)
  local b = string.byte(s, i)
  if not b then return nil end
  if b < 0x80 then return b, i + 1 end
  local need, cp
  if b >= 0xC2 and b <= 0xDF then
    need, cp = 1, b - 0xC0
  elseif b >= 0xE0 and b <= 0xEF then
    need, cp = 2, b - 0xE0
  elseif b >= 0xF0 and b <= 0xF4 then
    need, cp = 3, b - 0xF0
  else
    return nil
  end
  for k = 1, need do
    local nb = string.byte(s, i + k)
    if not nb or nb < 0x80 or nb > 0xBF then return nil end
    cp = cp * 64 + (nb - 0x80)
  end
  return cp, i + need + 1
end

function M.decode_at(s, i)
  return decode_at(s, i)
end

function M.len(s)
  if type(s) ~= "string" then return 0 end
  local count, i = 0, 1
  while i <= #s do
    local _, next_index = decode_at(s, i)
    if not next_index then
      i = i + 1
    else
      i = next_index
    end
    count = count + 1
  end
  return count
end

function M.codepoints(s)
  local i = 1
  return function()
    if i > #s then return nil end
    local cp, next_index = decode_at(s, i)
    if not next_index then
      i = i + 1
      return nil
    end
    i = next_index
    return cp
  end
end

function M.char(cp)
  if cp < 0x80 then
    return string.char(cp)
  elseif cp < 0x800 then
    return string.char(0xC0 + math.floor(cp / 64), 0x80 + cp % 64)
  elseif cp < 0x10000 then
    return string.char(0xE0 + math.floor(cp / 4096),
                       0x80 + math.floor(cp / 64) % 64,
                       0x80 + cp % 64)
  end
  return string.char(0xF0 + math.floor(cp / 262144),
                     0x80 + math.floor(cp / 4096) % 64,
                     0x80 + math.floor(cp / 64) % 64,
                     0x80 + cp % 64)
end

-- Byte offset of a scalar index (1-based); clamps to the string end.
function M.byte_offset(s, scalar_index)
  local i, count = 1, 1
  while i <= #s and count < scalar_index do
    local _, next_index = decode_at(s, i)
    i = next_index or (i + 1)
    count = count + 1
  end
  return i
end

return M
