import CoreGraphics
import Foundation
// pclick <pid> <move|down|up|click|drag> x y [x2 y2]  (global screen points)
let a = CommandLine.arguments
let pid = pid_t(Int32(a[1])!)
let kind = a[2]
let x = Double(a[3])!, y = Double(a[4])!
func post(_ t: CGEventType, _ px: Double, _ py: Double) {
  let e = CGEvent(mouseEventSource: nil, mouseType: t, mouseCursorPosition: CGPoint(x: px, y: py), mouseButton: .left)!
  e.postToPid(pid)
  usleep(30000)
}
switch kind {
case "move": post(.mouseMoved, x, y)
case "click": post(.mouseMoved, x, y); usleep(100000); post(.leftMouseDown, x, y); usleep(80000); post(.leftMouseUp, x, y)
case "drag":
  let x2 = Double(a[5])!, y2 = Double(a[6])!
  post(.mouseMoved, x, y); usleep(200000); post(.leftMouseDown, x, y)
  for i in 1...12 { let t = Double(i)/12; post(.leftMouseDragged, x+(x2-x)*t, y+(y2-y)*t); usleep(30000) }
  usleep(300000); post(.leftMouseUp, x2, y2)
default: break
}
