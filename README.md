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

### Run as a Windows Service (pre-login)

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
- Broker availability (online/offline) is published with retained messages on `<username>/health`

### Actions and topics

- Actions are named and matched by MQTT message content
- Expected topic is `mqttpowermanager/<username>/<action_name>`
- Example publish command (using mosquitto tools):

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -t "mqttpowermanager/alice/PC_Lock" -m "PRESS"
```

### License

This project is licensed under the GNU General Public License v3.0 see [LICENSE](LICENSE) for details


