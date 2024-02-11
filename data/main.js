
// Connect to the socket. Same URL, port 81.
let Socket;

document.addEventListener("click", clickEvent);

function clickEvent(event) {
	if (event.target.type == "button") {
		let msg = {
			gadget: "CMU",
			device: "btn",
			id: event.target.id
		}
		Socket.send(JSON.stringify(msg));
	} else console.log(event.target);
}

function init() {
	Socket = new WebSocket('ws://' + window.location.hostname + ':81');
	console.log(window.location.hostname);
	Socket.onmessage = function (evt) {
		parseCommand(evt);
	}
}

function parseCommand(evt) {
	const obj = JSON.parse(evt.data,);

	if ("sec" in obj) {
		document.getElementById("secTime").innerHTML = obj.sec;
		console.log(obj.TIME);
	}

	if ("min" in obj) {
		document.getElementById("minTime").innerHTML = obj.min;
		console.log(obj.TIME);
	}
};

window.onload = () => {
	init();
}


