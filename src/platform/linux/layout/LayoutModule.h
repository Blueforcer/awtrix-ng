#pragma once

namespace awtrix::layout {
inline constexpr const char* kModule = R"BERRY(
# Prepared layouts use the same specification as the JSON layout field. Native errors are
# returned as strings so C++ temporaries are destroyed before Berry raises the exception.
import json
import global
var layout = module('layout')
def _layout_prepare(spec) # layout.prepare(spec)
  var result = _native_layout_prepare(json.dump(spec))
  if type(result) == 'string' raise 'value_error', result end
  return result
end
def _layout_update(handle, spec) # layout.update(handle, spec)
  var result = _native_layout_update(handle, json.dump(spec))
  if type(result) == 'string' raise 'value_error', result end
  return result
end
def _layout_draw(handle) # layout.draw(handle)
  var result = _native_layout_draw(handle)
  if type(result) == 'string' raise 'value_error', result end
  return result
end
def _layout_release(handle) # layout.release(handle)
  return _native_layout_release(handle)
end
layout.prepare = _layout_prepare
layout.update = _layout_update
layout.draw = _layout_draw
layout.release = _layout_release

global.layout = layout
return layout
)BERRY";
}
