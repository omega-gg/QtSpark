import QtQuick
import QtQuick.Window
import QtQuick.Dialogs
import Splat 1.0

Window
{
    id: window

    width : 1280
    height: 800

    visible: true

    title: "Splat viewer" + (view.source != "" ? " | " + view.source.toString() : "")

    color: "black"

    // NOTE: Clears the source first, so opening the same file loads it again.
    function open(url)
    {
        view.source = ""
        view.source = url
    }

    SplatView
    {
        id: view

        anchors.fill: parent

        source: initialSource

        // NOTE: Keeps the GPU at speed, so movements start without a hitch.
        continuous: true

        // SPARK: examples/viewer, a 75 degree vertical field of view.
        fov: 75

        focus: true

        Component.onCompleted: forceActiveFocus()
    }

    DropArea
    {
        anchors.fill: parent

        onDropped: (drop) =>
        {
            if (drop.hasUrls) open(drop.urls[0])
        }
    }

    FileDialog
    {
        id: dialog

        nameFilters: [ "Splats (*.ply *.spz)", "All files (*)" ]

        onAccepted: open(selectedFile)
    }

    Rectangle
    {
        id: overlay

        anchors.left: parent.left
        anchors.top : parent.top

        anchors.margins: 8

        width : info.width  + 16
        height: info.height + 16

        radius: 4

        color: "#a0000000"

        Text
        {
            id: info

            anchors.centerIn: parent

            color: "white"

            font.family   : "monospace"
            font.pixelSize: 12

            text: statusText() + "\n"
                  + "backend  " + view.backendName + "\n"
                  + "splats   " + view.activeSplats + " / " + view.splatCount + "\n"
                  + "fps      " + view.fps.toFixed(1) + "\n"
                  + "metric   " + view.metricTime.toFixed(2) + " ms\n"
                  + "sort     " + view.sortTime.toFixed(2) + " ms\n"
                  + "load     " + view.loadTime.toFixed(0) + " ms\n"
                  + "camera   " + (orbit ? "orbit" : "fly") + "\n"
                  + "\n"
                  + "O open  Esc cancel  R reset  C fly/orbit  1/2/3 orientation\n"
                  + (orbit ? "Left drag orbit, Right or Shift+Left pan\n"
                           + "Middle drag or Wheel zoom"
                           : "WASD QE move, Shift x5, Ctrl /5\n"
                           + "Left drag rotate, Right/Middle slide, Wheel")

            property bool orbit: (view.controlMode == SplatView.Orbit)

            function statusText()
            {
                if (view.status == SplatView.Loading) return "Loading..."
                if (view.status == SplatView.Error)   return "Error: " + view.error
                if (view.status == SplatView.Null)    return "Drop a .ply or .spz file, or press O"
                return "Ready"
            }
        }
    }

    Shortcut
    {
        sequence: "O"

        onActivated: dialog.open()
    }

    // NOTE: Clearing the source aborts the load.
    Shortcut
    {
        sequence: "Escape"

        onActivated: if (view.status == SplatView.Loading) view.source = ""
    }

    Shortcut
    {
        sequence: "R"

        onActivated: view.resetCamera()
    }

    // NOTE: Switching starts the camera over from the start pose.
    Shortcut
    {
        sequence: "C"

        onActivated: view.controlMode = (view.controlMode == SplatView.Fly) ? SplatView.Orbit
                                                                           : SplatView.Fly
    }

    Shortcut { sequence: "1"; onActivated: view.orientation = SplatView.OpenCV }
    Shortcut { sequence: "2"; onActivated: view.orientation = SplatView.OpenGL }
    Shortcut { sequence: "3"; onActivated: view.orientation = SplatView.ZUp    }
}
