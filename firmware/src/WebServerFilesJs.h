#ifndef WEBSERVERFILEJS_H
#define WEBSERVERFILEJS_H

#include <pgmspace.h>

const char script_js[] PROGMEM = R"rawliteral(
document.addEventListener("DOMContentLoaded", function() {
	console.log("Connecting to WebSocket...");
	const ws = new WebSocket(`ws://${window.location.host}/ws`);
	ws.onopen = function() {
		console.log("WebSocket connection opened");
	};
	ws.onmessage = function(event) {
		console.log("WebSocket message received: ", event.data);
		const data = JSON.parse(event.data);
		const table = document.querySelector("#batteryStatusTable tbody");
		table.innerHTML = ""; // Clear existing rows
		data.batteryStatus.forEach((battery, index) => {
			const row = document.createElement("tr");
			row.innerHTML = `
				<td>Battery ${battery.index}</td>
				<td>${battery.voltage}</td>
				<td>${battery.current}</td>
				<td>${battery.ampereHour}</td>
				<td><span style='color:${battery.ledStatus};'>●</span></td>
				<td><button onclick="switchBattery(${battery.index}, true)">Switch On</button> <button onclick="switchBattery(${battery.index}, false)">Switch Off</button></td>
			`;
			table.appendChild(row);
		});
	};
	ws.onerror = function(error) {
		console.error("WebSocket error: ", error);
	};
	ws.onclose = function() {
		console.log("WebSocket connection closed");
	};
});

function switchBattery(index, state) {
	console.log(`Switching battery ${index} to ${state ? 'on' : 'off'}`);
	// Envoyer une requête POST pour éviter les mutations via URL query
	const xhr = new XMLHttpRequest();
	xhr.open("POST", `/switch_${state ? 'on' : 'off'}`, true);
	xhr.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
	const adminToken = window.localStorage.getItem("bmu_admin_token");
	if (adminToken) {
		xhr.setRequestHeader("X-BMU-Token", adminToken);
	}
	xhr.send(`battery=${encodeURIComponent(index)}`);
}
)rawliteral";

#endif // WEBSERVERFILEJS_H
