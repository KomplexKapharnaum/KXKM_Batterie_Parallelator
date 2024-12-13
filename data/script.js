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
    getValues()
    getConf()
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

    var myObj = JSON.parse(event.data);
    var keys = Object.keys(myObj);

    if (keys.includes("batteryStatus")) {
        updateBatteryStatus(myObj.batteryStatus);
    }

    if (keys.includes("controlSwitches")) {
        updateControlSwitches(myObj.controlSwitches);
    }
}

function updateBatteryStatus(batteryStatus) {
    var batteryStatusList = document.getElementById("batteryStatus");
    batteryStatusList.innerHTML = "";
    batteryStatus.forEach(function(status) {
        var li = document.createElement("li");
        li.innerHTML = `Battery ${status.index}: Voltage = ${status.voltage}V, Current = ${status.current}A, Ah = ${status.ampereHour} <span style="color:${status.ledStatus};">●</span>`;
        batteryStatusList.appendChild(li);
    });
}

function updateControlSwitches(controlSwitches) {
    var controlSwitchesList = document.getElementById("controlSwitches");
    controlSwitchesList.innerHTML = "";
    controlSwitches.forEach(function(switchControl) {
        var li = document.createElement("li");
        li.innerHTML = `Battery ${switchControl.index}: <a href="/switch_on?battery=${switchControl.index}">Switch On</a> | <a href="/switch_off?battery=${switchControl.index}">Switch Off</a>`;
        controlSwitchesList.appendChild(li);
    });
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