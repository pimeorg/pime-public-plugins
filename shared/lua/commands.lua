-- Output command builders (pime-core/schema/cbor/command.cddl names).

local M = {}

function M.commit(text)
  return { type = "commit", text = text }
end

function M.composition(text, cursor, segments)
  local command = { type = "set_composition", text = text,
                    cursor = cursor or #text }
  if segments then command.segments = segments end
  return command
end

function M.candidates(items)
  return { type = "show_candidates", items = items }
end

function M.hide()
  return { type = "hide_candidates" }
end

function M.notify(code)
  return { type = "notify", code = code }
end

function M.beep()
  return { type = "beep" }
end

function M.clear_ink()
  return { type = "clear_ink" }
end

function M.item(text, comment)
  local item = { text = text }
  if comment then
    item.comment = comment
    item.label = comment
  end
  return item
end

return M
