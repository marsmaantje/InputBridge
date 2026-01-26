# Design Proposal
This is a basic proposal/outline of how I envision the system would work, not everything is accounted for yet and it is all heavily subject to change.

```mermaid
graph TD;
idStart(Program Start)
idEnd(Program End)

idMain(Main Thread)
idUi(UI Loop)
idQuit(Send quit request)
idQuitWait(Wait for other threads to close<br>Max 5 seconds)

idInput(Device Input Thread)
idInputListen(Listen for new devices)
idInputConnectionStart(Listen to device input updates)
idInputListenQuit(Stop listening to device events)
idInputConnectionQuit(Stop all running haptics<br>Close device connection)

idNetworkOutput(Network Output Thread)
idNetworkOutputListen(Listen for incoming connections)
idNetworkOutputConnection(Handle Connection)
idNetworkOutputConnectionQuit(Close Connection)
idNetworkOutputQuit(Close Network Server)

idStart-->idMain

idMain--Starts-->idInput & idNetworkOutput

idMain-->idUi

subgraph Device Input
idInput-->idInputListen
idInputListen--New device connected-->idInputConnectionStart
idInputConnectionStart--Quit Request-->idInputConnectionQuit
idInputListen--Quit Request-->idInputListenQuit
end

subgraph Network Output
idNetworkOutput--Start Netowork Server-->idNetworkOutputListen
idNetworkOutputListen--Connection Opened-->idNetworkOutputConnection
idNetworkOutputConnection--Quit Request-->idNetworkOutputConnectionQuit
idNetworkOutputListen--Quit Request-->idNetworkOutputQuit
end

subgraph UI
idUi-->idQuit-->idQuitWait-->idEnd
end
```

Interfaces:
- IDeviceState
  - ButtonCount
    - uint
  - ButtonStateAray
    - bool
  - AxisCount
    - uint
  - AxisStateAray
    - axisValue
    - axisMinValue
    - axisMaxValue

Threads:
- Main
  - UI
  - IO Mapping
- DeviceInput
  - Scan devices
  - Listen to connected devices
  - Output device state to shared memory
  - Manage haptics playing on device
  - Read shared memory for what haptics to play
- NetworkOutputs
  - Listen for incoming connections
  - Supply connection with UI configured data
  - Parse connection data to haptic targets