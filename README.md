<p align="center">
  <img src="src/assets/mpm_icon.png" alt="MPM icon" width="128" />
</p>

## MPM - MQTT Power Manager

MQTT Power Manager desktop client built with Qt for Windows. It listens to MQTT messages and executes user defined actions on the local machine.

### Supported actions

- **Shutdown**
- **Restart**
- **Suspend**
- **Sleep**
- **Lock**
- **Open executable**: Select path to any .exe and open it with MQTT command

### Quick start

1. Open the app
2. Go to the Connection tab and set username, host, port, and credentials
3. Click Save Settings, then Connect
4. Go to Actions to add actions and expected messages
5. Use the list context menu to copy the topic or a ready to edit publish command
6. Go to Settings to enable Start with Windows, Auto connect, Start minimized, Lock startup path, or Add to Start Menu

### Installer

- Install [NSIS](https://nsis.sourceforge.io/) and build in Release; this creates `MPM-Setup.exe` (and `MPM.zip` if WinRAR is installed) next to the `Release` output folder
- The installer works in any folder and preselects an existing installation, which is upgraded in place
- It closes MPM, stops the service, replaces the files, and installs or updates `MPMService` (uncheck the service component to remove it)
- Uninstalling keeps your settings in `C:\ProgramData\MPM` unless you choose to delete them

### Run as a Windows Service (pre-login)

- The installer sets up the service for you; the steps below are for a manual setup
- Build the project; two executables are produced: `MPM.exe` (GUI) and `MPMService.exe` (service/console)
- Configure your connection and actions in the GUI first and click Save (stored in `C:\ProgramData\MPM\MqttPowerManager.ini`)
- Install the service (run from elevated PowerShell or cmd):
```powershell
MPMService.exe --install
```
- Start the service from Services.msc or with:
```powershell
sc start MPMService
```
- Uninstall the service:
```powershell
sc stop MPMService
MPMService.exe --uninstall
```

Notes:
- Passwords are protected using DPAPI with machine scope so the service can read them
- Broker availability (online/offline) is published with retained messages on `mqttpowermanager/<username>/health`

### Actions and topics

- Actions are named and matched by MQTT message content
- Retained messages are ignored, so publish commands without the retain flag
- Expected topic is `mqttpowermanager/<username>/<action_name>`
- Example publish command (using mosquitto tools):

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -t "mqttpowermanager/alice/PC_Lock" -m "PRESS"
```

### Home Assistant

- Enable **Home Assistant discovery** on the Actions tab to show every action as a button on an `MPM <username>` device (needs the MQTT integration)
- Buttons are published on connect, when Home Assistant restarts, and whenever actions are added, edited, or deleted; deleted actions are removed from Home Assistant
- Click **Sync to Home Assistant** to publish now; turning discovery off removes the buttons
- Set **Device name** to rename the device in Home Assistant (default `MPM <username>`)
- The device shows as unavailable while MPM is offline

### License

This project is licensed under the GNU General Public License v3.0 see [LICENSE](LICENSE) for details


