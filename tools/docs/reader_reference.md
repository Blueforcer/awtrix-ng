# Reference answers

One answer per reader task. They must pass on every clock; tests/linux/test_docs_panels.py
checks that, so a task or a check that cannot pass shows up at once.

## headline-scrolls

```berry
class News
  def draw()
    scroll_text("AWTRIX NG now reads the news from your smart home", 0xFFFFFF)
  end
end
return News()
```

## value-beside-sun

```berry
class Room
  def draw()
    circle_fill(3, 3, 3, 0xFFC000)
    scroll_text(9, 6, width() - 9, "Living room 21.5 degrees", 0xFFFFFF)
  end
end
return Room()
```

## centered-degrees

```berry
class Degrees
  def draw()
    var s = "21°"
    text((width() - text_ink_width(s)) / 2, 6, s, 0xFFFFFF)
  end
end
return Degrees()
```

## large-at-top

```berry
class Big
  def draw()
    font("large")
    text(1, 6, "BIG", 0xFFFFFF)
  end
end
return Big()
```

## pushed-long-text

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/news \
  -H 'Content-Type: application/json' \
  -d '{"text":"Breaking news from the smart home: the washing machine is done"}'
```

## draw-label

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/box \
  -H 'Content-Type: application/json' \
  -d '{"draw":[["rect",0,0,32,8,"#FFFFFF"],["text",2,2,"12:30","#FFFFFF"]]}'
```

## text-cut-not-moved

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/label \
  -H 'Content-Type: application/json' \
  -d '{"text":"This text is far too long for the display","scroll":"static"}'
```

## notification-until-read

```bash
curl -X POST http://<awtrix-ip>/api/v1/notifications \
  -H 'Content-Type: application/json' \
  -d '{"text":"Your washing machine is done, please take the laundry out","repeat":1}'
```

## two-colours

```berry
class Cpu
  var parts
  def init()
    self.parts = [["CPU ", 0x888888], ["42%", 0x00FF00]]
  end
  def draw()
    text(1, 6, self.parts)
  end
end
return Cpu()
```

## go-moves-by-itself

```berry
class Go
  def draw()
    var x = width() - (now_ms() / 40) % (width() + 12)
    text(x, 6, "GO", 0x00FF00)
  end
end
return Go()
```

## progress-beside-sun

```berry
class Load
  def draw()
    circle_fill(3, 3, 3, 0xFFC000)
    progress(64, 0x00FF00, 0x202020, 9)
  end
end
return Load()
```

## two-moving-lines

```berry
class Two
  def draw()
    scroll_text(0, 6, width(), "First line, long enough that it has to move", 0xFFFFFF)
    scroll_text(0, 14, width(), "Second line, long enough that it has to move", 0x00AAFF)
  end
end
return Two()
```

## still-title-moving-value

```berry
class Outside
  def draw()
    var t = "OUTSIDE"
    text((width() - text_ink_width(t)) / 2, 6, t, 0xFFFFFF)
    scroll_text(0, 14, width(), "Temperature 21.5 degrees, humidity 40 percent", 0x00AAFF)
  end
end
return Outside()
```

## layout-title-and-value

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/outside \
  -H 'Content-Type: application/json' \
  -d '{"layout":{"version":1,"regions":[{"id":"title","box":[0,0,52,8],"text":"OUTSIDE"},{"id":"value","box":[0,8,52,8],"text":"Temperature 21.5 degrees, humidity 40 percent"}]}}'
```

## enlarged-corner

```bash
curl -X PUT http://<awtrix-ip>/api/v1/apps/pushed/corner \
  -H 'Content-Type: application/json' \
  -d '{"draw":[["pixel",25,7,"#FF0000"]]}'
```
