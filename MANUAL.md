# QtSpark manual

QtSpark shows 3D Gaussian splats, the photo-real scenes captured from photos or video, and lets you
fly through them. It is [Spark](https://github.com/sparkjsdev/spark) rewritten for Qt: the same
picture as Spark in a browser, drawn natively by your graphics card.

It comes in two parts. **splatviewer** is a small application that opens a splat file and lets you
look around it. **SplatView** is the QML item that does the drawing, which you can put in your own
Qt Quick application. The first half of this manual is about the viewer, the second about the item.

## First run

Unzip the build for your system and start the viewer:

| System | Start |
| --- | --- |
| Windows | `splatviewer.exe` |
| macOS | `splatviewer.app` |
| Linux | `start.sh`, which points the viewer at the libraries next to it |

The window opens black, with a panel in the top left corner reading
`"Drop a .ply or .spz file, or press O"`. Do either: drag a file onto the window, or press `O` and
pick one. You can also give the file when you start it:

    splatviewer scene.spz

The panel reads `"Loading..."` while the file is read, then `"Ready"`, and the scene appears in
front of you. Move with W A S D and drag with the left button to look around. The rest of this
manual is the detail.

## Files

QtSpark opens the two formats Spark opens without extra libraries:

- **`.ply`**: the standard 3DGS export of most capture tools, with spherical harmonics up to degree
  3, the compressed PLY of SuperSplat, and plain point clouds.
- **`.spz`**: Niantic's compressed format, versions 1 to 3.

The type is read from the file itself, so a wrong extension does not matter.

Opening a file replaces the scene on screen and frees the one before it. The camera goes back to
its start, as if the window had just opened. Opening the same file again reloads it from disk.

A large file takes a moment to read. Press `Esc` while the panel reads `"Loading..."` to abandon
it; opening another file abandons it too.

## The panel

The panel in the top left corner always shows what the viewer is doing:

| Line | What it says |
| --- | --- |
| first line | `"Ready"`, `"Loading..."`, an error, or the invitation to open a file |
| `backend` | the graphics API drawing the scene, see `Graphics backends` |
| `splats` | the splats drawn now, over the splats in the file |
| `fps` | frames drawn per second, while the view changes |
| `metric` | how long the last depth readback took, in milliseconds |
| `sort` | how long the last sort of the splats took, in milliseconds |
| `load` | how long the file took to open, in milliseconds |
| `camera` | `fly` or `orbit`, see below |

Under them, a reminder of the keys for the current camera mode.

Splats are drawn back to front, and the order is sorted again each time you move. `metric` and
`sort` are the two steps of that, and while they run the picture still shows the order from a
moment ago. That is what Spark does too, and it is why a fast turn can show a frame or two of soft,
misordered splats before they settle.

## Flying

The viewer starts in fly mode, the default controls of Spark: you move like a camera in a game.
It starts one unit in front of the origin of the scene, looking at it, where Spark's viewer
starts.

| Key | Action |
| --- | --- |
| W, A, S, D, or the arrows | forward, left, back, right |
| E, or Page Up | up |
| Q, or Page Down | down |
| Home, End | look up, look down |
| Insert, Delete | turn left, turn right |
| `Shift` held | five times faster |
| `Ctrl` held | five times slower |
| `Caps Lock` held | ten times faster |

The movement letters are keys by position, not by what is printed on them, as in Spark. On a
French AZERTY keyboard they are Z Q S D, with A and E for down and up.

The mouse:

| Mouse | Action |
| --- | --- |
| Left drag | look around, carrying on a little after you let go |
| Right drag | slide in the plane of the screen |
| Middle drag | slide along the ground |
| Wheel | move forward or back |

`R` puts the camera back where it started.

## Orbiting

Press `C` to switch to orbit mode, and again to go back. Orbit is the camera of Spark's editor: it
circles a center, the origin of the scene to begin with, which suits a single object better than
flying does. Switching puts the camera back at its start.

| Mouse | Action |
| --- | --- |
| Left drag | circle around the center |
| Right drag, or `Shift` or `Ctrl` with the left drag | move the center in the plane of the screen |
| Middle drag, or the wheel | come closer or go away, also while dragging |

The camera stays between a tenth of a unit and ten units from the center. The keys do not move the
camera in orbit mode.

## Orientation

Capture tools do not agree on which way is up. If the scene shows upside down or on its side, press
`1`, `2` or `3`:

| Key | Orientation | For |
| --- | --- | --- |
| `1` | OpenCV, Y down | most 3DGS captures. The default, as in Spark's examples |
| `2` | OpenGL, Y up | files already in the usual 3D convention |
| `3` | Z up | files from tools where Z is up |

## Graphics backends

QtSpark draws with whatever graphics API Qt picks for your system, Direct3D 11 on Windows, Metal on
macOS and OpenGL on Linux. The `backend` line of the panel says which. Choose another with
`--backend`:

    splatviewer scene.spz --backend vulkan

| Value | API | Where |
| --- | --- | --- |
| `d3d11` | Direct3D 11 | Windows |
| `d3d12` | Direct3D 12 | Windows, with the MSVC build only |
| `vulkan` | Vulkan | Windows and Linux, with a Vulkan driver |
| `metal` | Metal | macOS |
| `opengl` | OpenGL 4.3 | Windows and Linux |

They all draw the same picture. On a Windows laptop with two graphics cards, the viewer asks for
the dedicated one.

## Command line

    splatviewer [file] [options]

| Option | What it does |
| --- | --- |
| `file` | a `.ply` or `.spz` to open at start |
| `--backend <name>` | the graphics API, see above |
| `--fast` | turns on the QtSpark performance options, see `fast` below |
| `--quit-after <ms>` | closes the window after that many milliseconds |
| `--screenshot <file>` | saves the window to that image just before it closes |
| `--help` | lists every option |

`--quit-after` and `--screenshot` together take a picture of a scene without anyone at the
keyboard. The other options, `--bench` and those after it in `--help`, run the benchmark against
Spark; [doc/benchmark.md](doc/benchmark.md) explains them.

The viewer is a windowed application and prints nothing on Windows. To read its messages in a
terminal, set `QT_FORCE_STDERR_LOGGING=1` first.

## When it goes wrong

A file that cannot be read shows `"Error: "` and the reason in the first line of the panel. The
scene that was there before is gone.

| Message | What it means |
| --- | --- |
| `"Unknown file type"` | Not a PLY or an SPZ, whatever its extension says. |
| `"Failed to open ..."` | The file is missing or cannot be read. |
| `"Invalid PLY header"`, `"Unsupported PLY ..."`, `"Missing ... property"` | A PLY that is damaged, or holds something else than splats. |
| `"Truncated gzip stream"`, `"Incomplete SPZ stream"` | An SPZ cut short, often a download that did not finish. |
| `"Too many splats: ..."` | A header claiming billions of splats, a damaged file. |

If the window stays black with `"Ready"`, the camera may be inside the scene or facing away from
it: press `R`, then try the orientations.

## Using SplatView in your application

The viewer is a few lines of QML around one item:

```qml
import QtQuick
import Splat 1.0

Window {
    width: 1280
    height: 800
    visible: true

    SplatView {
        anchors.fill: parent

        source: "file:///path/scene.spz"

        focus: true
    }
}
```

`focus: true` gives the item the keyboard from the start. Without it, the item takes the keyboard
when it is clicked.

### Setting it up

Link the `Splat` library, include `WSplatView.h`, and register the type before you load your QML:

```cpp
qmlRegisterType<WSplatView>("Splat", 1, 0, "SplatView");
```

If your application runs on OpenGL, ask for a 4.3 core context before the application object is
created, as `viewer/main.cpp` does. The shaders need it, and Qt asks for an older one by default:

```cpp
QSurfaceFormat format = QSurfaceFormat::defaultFormat();

format.setVersion(4, 3);
format.setProfile(QSurfaceFormat::CoreProfile);

QSurfaceFormat::setDefaultFormat(format);
```

The backend is Qt Quick's: set it with `QQuickWindow::setGraphicsApi`, or with the
`QSG_RHI_BACKEND` environment variable.

### Properties

| Property | Default | What it does |
| --- | --- | --- |
| `source` | empty | The file to show: a `file:` or `qrc:` URL. Setting another loads it and frees the old scene. Clearing it abandons a load in progress. |
| `backgroundColor` | black | What is behind the splats. A translucent color, `"transparent"` included, lets the items behind the view show through. |
| `orientation` | `SplatView.OpenCV` | `OpenCV`, `OpenGL` or `ZUp`, see `Orientation`. |
| `controlMode` | `SplatView.Fly` | `Fly` or `Orbit`. Changing it puts the camera back at its start. |
| `controlsEnabled` | `true` | Off, the item ignores the mouse and the keys and passes them on. |
| `moveSpeed` | `1` | Units per second in fly mode, before `Shift` and `Ctrl`. |
| `cameraPosition` | `(0, 0, 1)` | Where the camera is. |
| `cameraRotation` | identity | Which way it looks, as a quaternion. The identity looks down -Z, at the origin. |
| `fov` | `60` | The vertical field of view, in degrees. The viewer sets 75, like Spark's viewer. |
| `zNear`, `zFar` | `0.01`, `1000` | The nearest and farthest distances drawn. |
| `shDegree` | `3` | The highest degree of spherical harmonics used, 0 for flat colors. Lower is faster. |
| `maxStdDev` | `2.83` | How far out each splat is drawn, in standard deviations. |
| `blurAmount` | `0.3` | The blur added to every splat, which keeps small ones from flickering. |
| `minAlpha` | `0.002` | Splats fainter than this are not drawn. |
| `fast` | `false` | The QtSpark performance options: a sort metric computed on the processor, a faster multithreaded sort, and no work for splats smaller than a pixel. The picture stays the same, and it is only meant for scenes that do not change. Off, the item runs exactly Spark's code path. |
| `continuous` | `false` | Draws every frame, like Spark, instead of only when something changes. On a laptop it keeps the graphics card awake, so movements start without a hitch, at the cost of power. The viewer turns it on. |

The render settings keep Spark's defaults, so a scene looks the same in both.

Read only:

| Property | What it says |
| --- | --- |
| `status` | `SplatView.Null` (nothing to show), `Loading`, `Ready` or `Error` |
| `error` | why the last load failed |
| `splatCount` | the splats in the file |
| `activeSplats` | the splats drawn now |
| `fps` | frames per second |
| `metricTime`, `sortTime` | the last depth readback and sort, in milliseconds |
| `loadTime` | how long the file took to open, in milliseconds |
| `backendName` | the graphics API, `"D3D11"` for instance |

### Methods

- `resetCamera()` puts the camera back at its start, and the orbit center back at the origin.
- `lookAt(point)` turns the camera toward a point, without moving it.
- `statistics()` returns the counters of the renderer as a map, for benchmarks.

## Keyboard shortcuts

These are the viewer's. In your own application only the movement keys come with SplatView.

| Key | What it does |
| --- | --- |
| `O` | Opens a file |
| `Esc` | Abandons a load in progress |
| `R` | Puts the camera back at its start |
| `C` | Switches between fly and orbit |
| `1`, `2`, `3` | OpenCV, OpenGL or Z up orientation |
| W A S D, arrows | Move, in fly mode |
| Q E, Page Down Page Up | Down and up, in fly mode |
| Home End, Insert Delete | Look up and down, turn left and right, in fly mode |
| `Shift`, `Ctrl`, `Caps Lock` | Five times faster, five times slower, ten times faster |
