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

    for (var i = 0; i < keys.length; i++){
        var key = keys[i];
        console.log(key + " " + myObj[key])
        
        if (key.startsWith("slider")){
            document.getElementById(key).innerHTML = myObj[key];
            document.getElementById("slider"+ (i+1).toString()).value = myObj[key];
        }
        else {
            if (document.getElementById(key))
                document.getElementById(key).value = myObj[key];
        }
    }
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