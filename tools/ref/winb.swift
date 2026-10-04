import AppKit
import CoreGraphics
// winb: "<window id> <pid> <x> <y> <w> <h> <backing scale>" of the Slay the Spire 2 window (screen points)
let list = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as! [[String: Any]]
let scale = NSScreen.main?.backingScaleFactor ?? 2
for w in list where (w[kCGWindowOwnerName as String] as? String ?? "").contains("Slay the Spire") {
  let b = w[kCGWindowBounds as String] as! [String: Any]
  if (b["Height"] as! Double) > 100 && (b["Width"] as! Double) > 600 {
    print(w[kCGWindowNumber as String]!, w[kCGWindowOwnerPID as String]!, b["X"]!, b["Y"]!, b["Width"]!, b["Height"]!, scale)
    break
  }
}
