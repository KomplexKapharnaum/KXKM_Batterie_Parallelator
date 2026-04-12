// KXKM Batterie Parallelator — ESP-IDF Web UI
// WebSocket sur /ws (meme port), mutations POST avec token JSON

document.addEventListener("DOMContentLoaded", function() {
    connectWebSocket();
});

function connectWebSocket() {
    console.log("Connecting to WebSocket...");
    const ws = new WebSocket('ws://' + location.host + '/ws');

    ws.onopen = function() {
        console.log("WebSocket connection opened");
        const token = document.getElementById('token-input')?.value || '';
        ws.send(JSON.stringify({ auth: token }));
    };

    ws.onmessage = function(event) {
        console.log("WebSocket message received: ", event.data);
        const data = JSON.parse(event.data);
        updateBatteryDisplay(data);
    };

    ws.onerror = function(error) {
        console.error("WebSocket error: ", error);
    };

    ws.onclose = function() {
        console.log("WebSocket connection closed, reconnecting in 3s...");
        setTimeout(connectWebSocket, 3000);
    };
}

function updateBatteryDisplay(data) {
    const table = document.querySelector("#batteryStatusTable tbody");
    table.innerHTML = "";
    data.batteryStatus.forEach(function(battery) {
        const row = document.createElement("tr");
        row.innerHTML =
            '<td>Battery ' + battery.index + '</td>' +
            '<td>' + battery.voltage + '</td>' +
            '<td>' + battery.current + '</td>' +
            '<td>' + battery.ampereHour + '</td>' +
            '<td><span style="color:' + battery.ledStatus + ';">&#9679;</span></td>' +
            '<td>' +
                '<button onclick="switchBattery(' + battery.index + ', true)">Switch On</button> ' +
                '<button onclick="switchBattery(' + battery.index + ', false)">Switch Off</button>' +
            '</td>';
        table.appendChild(row);
    });
}

function switchBattery(index, state) {
    console.log('Switching battery ' + index + ' to ' + (state ? 'on' : 'off'));
    const token = document.getElementById('token-input')?.value || '';
    fetch('/api/battery/switch_' + (state ? 'on' : 'off'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ battery: index, token: token })
    })
    .then(function(r) { return r.text(); })
    .then(function(t) { console.log(t); })
    .catch(function(e) { console.error(e); });
}
