var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
window.addEventListener('load', onload);

function onload(event) {
    initWebSocket();
}

function getValues(){
    websocket.send("getValues");
}

function getConf(){
  websocket.send("getConf");
}

function initWebSocket() {
    console.log('Trying to open a WebSocket connection…');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
}

function onOpen(event) {
    console.log('Connection opened');
    getValues();
    getConf();
    setInterval(getValues, 5000); // Mettre à jour toutes les 5 secondes
}

function onClose(event) {
    console.log('Connection closed');
    setTimeout(initWebSocket, 5000);
}

function onMessage(event) {
    console.log("WS rcv: " + event.data)

    if (event.data == "reset") {
        console.log("reset")
        setTimeout(function(){ 
            // force reload page
            window.location.reload(true);
        }, 1000);
        document.getElementById("content").innerHTML = "Saving & Restarting ...";
        return;
    }

    var myObj = JSON.parse(event.data); // Utilisation de JSON pour parser les données reçues
    var keys = Object.keys(myObj);

    if (keys.includes("batteryStatus")) {
        updateBatteryStatus(myObj.batteryStatus);
    }

    if (keys.includes("controlSwitches")) {
        updateControlSwitches(myObj.controlSwitches);
    }
}

function updateBatteryStatus(batteryStatus) {
    const batteryStatusTable = document.getElementById("batteryStatusTable").getElementsByTagName('tbody')[0];
    batteryStatusTable.innerHTML = batteryStatus.map(status => `
        <tr>
            <td>Battery ${status.index}</td>
            <td>${status.voltage}</td>
            <td>${status.current}</td>
            <td>${status.ampereHour}</td>
            <td><span style="color:${status.ledStatus};">●</span></td>
            <td>
                <button onclick="switchBattery(${status.index}, true)">Switch On</button>
                <button onclick="switchBattery(${status.index}, false)">Switch Off</button>
            </td>
        </tr>
    `).join('');
}

function switchBattery(index, state) {
    const action = state ? 'switch_on' : 'switch_off';
    fetch(`/${action}?battery=${index}`, { method: 'GET' })
        .then(response => response.text())
        .then(data => {
            console.log(data);
            setTimeout(getValues, 10); // Mettre à jour les valeurs après 10 ms
        })
        .catch(error => console.error('Error:', error));
}

var newConf = {};

function updateConf(element) {
  newConf[element.id] = document.getElementById(element.id).value;
}

function saveConf() 
{
    let data = JSON.stringify(newConf);
    console.log("conf update: " + data);
    websocket.send("conf" + data);
    newConf = {};
}