const STATUS = {
    OK: 200,
    ACCEPTED: 202,
    BAD_REQUEST: 400,
    NOT_FOUND: 404,
    INTERNAL_SERVER_ERROR: 500
};
const timerStatus = {
    STOPPED: 0,
    RUNNING: 1
}
const gameStatus = {
    IN_PROGRESS: 0,
    EXTENDED_TIME: 1,
    HALFTIME: 2
}
const REQUEST_PERIOD = 250;

const dotStatus = document.getElementById('dot-status');
const btnUpVisitor = document.getElementById('up-visitor');
const btnDownVisitor = document.getElementById('down-visitor');
const btnUpLocal = document.getElementById('up-local');
const btnDownLocal = document.getElementById('down-local');
const btnUpChukker = document.getElementById('up-chukker');
const btnDownChukker = document.getElementById('down-chukker');
const btnStartTimer = document.getElementById('start-timer');
const btnStopTimer = document.getElementById('stop-timer');
const btnResetTimer = document.getElementById('reset-timer');
const btnResetAll = document.getElementById('reset-all');
const btnSetDefaultTimer = document.getElementById('set-default-timer');
const btnUpMinute = document.getElementById('up-minute');
const btnDownMinute = document.getElementById('down-minute');
const btnUpSecond = document.getElementById('up-second');
const btnDownSecond = document.getElementById('down-second');
const DOMAIN = "polopoints.local";
let refreshTimer = null;
const localValue = document.getElementById('local');
const visitorValue = document.getElementById('visitor');
const chukkerValue = document.getElementById('chukker');
const timerMMValue = document.getElementById('timer-mm');
const timerSSValue = document.getElementById('timer-ss');
const gameStateTitle = document.getElementById('game-state-title');
let timerState = timerStatus.STOPPED;
let gameState = gameStatus.IN_PROGRESS;
let worker = new Worker('worker.js');

localValue.value = 1;
chukkerValue.value = 2;
visitorValue.value = 3;
timerMMValue.value = 11;
timerSSValue.value = 22;

const command = {
    INC_SCORE_LOCAL: 1,
    INC_SCORE_VISITOR: 2,
    DEC_SCORE_LOCAL: 3,
    DEC_SCORE_VISITOR: 4,
    INC_CHUKKER: 5,
    DEC_CHUKKER: 6,
    START_TIMER: 7,
    STOP_TIMER: 8,
    RESET_TIMER: 9,
    SET_CURRENT_TIMER: 10,
    SET_DEFAULT_TIMER: 11,
    SET_EXTENDED_TIMER: 12,
    SET_HALFTIME_TIMER: 13,
    RESET_ALL: 14
};

const dataIndex = {
    LOCAL: 0,
    VISITOR: 1,
    CHUKKER: 2,
    TIMER_MM: 3,
    TIMER_SS: 4,
    TIMER_STATE: 5,
    GAME_STATE: 6
}

/* -------------------------------------------------------------------------------------------------------------- */
/* -------------------------------------------------- EVENTS ---------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------------------- */
// Visitor
btnUpVisitor.addEventListener('click', async () => {
    const rawResponse = await fetch(`http://${DOMAIN}/score?cmd=${command.INC_SCORE_VISITOR}`);
    if (rawResponse.status === STATUS.ACCEPTED) {
        const response = await rawResponse.text();
        setScoreboardValues(response);
    }
});

btnDownVisitor.addEventListener('click', async () => {
    const rawResponse = await fetch(`http://${DOMAIN}/score?cmd=${command.DEC_SCORE_VISITOR}`);
    if (rawResponse.status === STATUS.ACCEPTED) {
        const response = await rawResponse.text();
        setScoreboardValues(response);
    }
});

// Local
btnUpLocal.addEventListener('click', async () => {
    const rawResponse = await fetch(`http://${DOMAIN}/score?cmd=${command.INC_SCORE_LOCAL}`);
    if (rawResponse.status === STATUS.ACCEPTED) {
        const response = await rawResponse.text();
        setScoreboardValues(response);
    }
});

btnDownLocal.addEventListener('click', async () => {
    const rawResponse = await fetch(`http://${DOMAIN}/score?cmd=${command.DEC_SCORE_LOCAL}`);
    if (rawResponse.status === STATUS.ACCEPTED) {
        const response = await rawResponse.text();
        setScoreboardValues(response);
    }
});

// chukker
btnUpChukker.addEventListener('click', async () => {
    const rawResponse = await fetch(`http://${DOMAIN}/chukker?cmd=${command.INC_CHUKKER}`);
    if (rawResponse.status === STATUS.ACCEPTED) {
        const response = await rawResponse.text();
        setScoreboardValues(response);
        if(gameState === gameStatus.IN_PROGRESS) gameStateTitle.innerText = "CHUKKER " + chukkerValue.value;
    }
});

btnDownChukker.addEventListener('click', async () => {
    const rawResponse = await fetch(`http://${DOMAIN}/chukker?cmd=${command.DEC_CHUKKER}`);
    if (rawResponse.status === STATUS.ACCEPTED) {
        const response = await rawResponse.text();
        setScoreboardValues(response);
        if(gameState === gameStatus.IN_PROGRESS) gameStateTitle.innerText = "CHUKKER " + chukkerValue.value;
    }
});

// Timer
btnStartTimer.addEventListener('click', async () => {
    if (parseInt(timerMMValue.value) === 0 && parseInt(timerSSValue.value) === 0) {
        alert('El timer no puede ser 00:00.');
        return;
    }
    else {
        const rawResponse = await fetch(`http://${DOMAIN}/timer?cmd=${command.START_TIMER}`);

        if (rawResponse.status === STATUS.ACCEPTED) {
            worker.postMessage("stop-server-ping");
            if (refreshTimer === null) { startAutoRequest(); }
        }
        else { alert('Something went wrong.'); }
    }
});

btnStopTimer.addEventListener('click', async () => {
    if (timerState === timerStatus.RUNNING) {
        const rawResponse = await fetch(`http://${DOMAIN}/timer?cmd=${command.STOP_TIMER}`);
        if (rawResponse.status !== STATUS.ACCEPTED) { alert('Something went wrong.'); }
        else{ worker.postMessage("start-server-ping"); }
    }
});

btnUpMinute.addEventListener('click', async () => {
    let mmValue = parseInt(timerMMValue.value) + 1;
    if(mmValue > 59) mmValue = 0;
    let ssValue = parseInt(timerSSValue.value);
    sendTimerData(mmValue, ssValue, command.SET_CURRENT_TIMER);
});

btnDownMinute.addEventListener('click', async () => {
    let mmValue = parseInt(timerMMValue.value) - 1;
    if(mmValue < 0) mmValue = 59;
    let ssValue = parseInt(timerSSValue.value);
    sendTimerData(mmValue, ssValue, command.SET_CURRENT_TIMER);
});

btnUpSecond.addEventListener('click', async () => {
    let mmValue = parseInt(timerMMValue.value);
    let ssValue = parseInt(timerSSValue.value) + 1;
    if(ssValue > 59) ssValue = 0;
    sendTimerData(mmValue, ssValue, command.SET_CURRENT_TIMER);
});

btnDownSecond.addEventListener('click', async () => {
    let mmValue = parseInt(timerMMValue.value);
    let ssValue = parseInt(timerSSValue.value) - 1;
    if(ssValue < 0) ssValue = 59;
    sendTimerData(mmValue, ssValue, command.SET_CURRENT_TIMER);
});

btnSetDefaultTimer.addEventListener('click', async () => {
    let mmValue = parseInt(timerMMValue.value);
    let ssValue = parseInt(timerSSValue.value);
    if (mmValue === 0 && ssValue === 0) { alert('El timer no puede ser 00:00.'); }
    else {
        switch(gameState){
            case gameStatus.IN_PROGRESS:
                sendTimerData(mmValue, ssValue, command.SET_DEFAULT_TIMER);
                break;
            case gameStatus.EXTENDED_TIME:
                sendTimerData(mmValue, ssValue, command.SET_EXTENDED_TIMER);
                break;
            case gameStatus.HALFTIME:
                sendTimerData(mmValue, ssValue, command.SET_HALFTIME_TIMER);
                break;
        }
    }
});

btnResetTimer.addEventListener('click', async () => {
    const rawResponse = await fetch(`http://${DOMAIN}/timer?cmd=${command.RESET_TIMER}`);
    if (rawResponse.status === STATUS.ACCEPTED) {
        const response = await rawResponse.text();
        setScoreboardValues(response);
        setOptions();
        stopAutoRequest();
    }
    else { alert('Something went wrong.'); }
});

// Reset all
btnResetAll.addEventListener('click', async () => {
    const rawResponse = await fetch(`http://${DOMAIN}/reset`);
    if (rawResponse.status === STATUS.ACCEPTED) {
        const response = await rawResponse.text();
        setScoreboardValues(response);
        setOptions();
        stopAutoRequest();
    }
    else { alert('Something went wrong.'); }
});

// Ping events
worker.addEventListener('message', function(e) {
    switch(e.data){
        case "connected":
            dotStatus.style.backgroundColor = "#32CD32";
            break;
        case "disconnected":
            dotStatus.style.backgroundColor = "red";
            break;
        default:
            dotStatus.style.backgroundColor = "red";
            break;
    }
  })

/* -------------------------------------------------------------------------------------------------------------- */
/* --------------------------------------------------- MAIN ----------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------------------- */

// Al cargarse el sitio web, buscar datos del tablero.
window.addEventListener('load', async () => {
    await refreshScoreboard();
    if (timerState === timerStatus.RUNNING) {
        worker.postMessage("stop-server-ping");
        if (refreshTimer === null) { startAutoRequest(); }
    }
    else { worker.postMessage("start-server-ping"); }
});

// Solicitar datos y refrescar el tablero
async function refreshScoreboard() {
    try{
        const controller = new AbortController();
        const timeoutId = setTimeout(() => controller.abort(), RESPONSE_TIMEOUT);
        const rawResponse = await fetch(`http://${DOMAIN}/scoreboard`, { signal: controller.signal });
        if (rawResponse.status === STATUS.OK) {
            const response = await rawResponse.text();
            setScoreboardValues(response);
            setOptions();
            if (timerState === timerStatus.STOPPED) { stopAutoRequest(); }
        }
        dotStatus.style.backgroundColor = "#32CD32";
        clearInterval(timeoutId);
    }
    catch(error){
        dotStatus.style.backgroundColor = "red";
    }
}

// Se configura un periodo de XX ms en los cuales se consulta el estado del tablero
function startAutoRequest() {
    refreshTimer = setInterval(async () => {
        await refreshScoreboard();
    }, REQUEST_PERIOD);
}

// Detener el autorequest de datos de tablero
function stopAutoRequest() {
    clearInterval(refreshTimer);
    refreshTimer = null;
}

// Plasmar datos en tablero
function setScoreboardValues(dataString) {
    const data = dataString.split(',');
    localValue.value = data[dataIndex.LOCAL].padStart(2, '0');
    visitorValue.value = data[dataIndex.VISITOR].padStart(2, '0');
    chukkerValue.value = data[dataIndex.CHUKKER];
    timerMMValue.value = data[dataIndex.TIMER_MM].padStart(2, '0');
    timerSSValue.value = data[dataIndex.TIMER_SS].padStart(2, '0');
    timerState = parseInt(data[dataIndex.TIMER_STATE]);
    gameState = parseInt(data[dataIndex.GAME_STATE]);
}

// Enviar valores de timer a servidor
async function sendTimerData(mm, ss, cmd) {
    const rawResponse = await fetch(`http://${DOMAIN}/timer/set?mm=${mm}&ss=${ss}&cmd=${cmd}}`);
    if (rawResponse.status === STATUS.ACCEPTED) {
        const response = await rawResponse.text();
        setScoreboardValues(response);
        setOptions();
        if (cmd === command.SET_DEFAULT_TIMER) { alert('Duración de chukker actualizado.'); }
        if (cmd === command.SET_EXTENDED_TIMER) { alert('Tiempo extendido actualizado.'); }
        if (cmd === command.SET_HALFTIME_TIMER) { alert('Tiempo de intervalo actualizado.'); }
    }
}

// Fijar opciones en front-end segun estado de timer
function setOptions() {
    switch(gameState){
        case gameStatus.IN_PROGRESS:
            gameStateTitle.style.backgroundColor = "#9ACD32";
            gameStateTitle.style.color = "black";
            gameStateTitle.innerText = "CHUKKER " + chukkerValue.value;
            break;
        case gameStatus.HALFTIME:
            gameStateTitle.style.backgroundColor = "#FFFACD";
            gameStateTitle.style.color = "#FF6347";
            gameStateTitle.innerText = "INTERVALO";
            break;
        case gameStatus.EXTENDED_TIME:
            gameStateTitle.style.backgroundColor = "#87CEEB";
            gameStateTitle.style.color = "#00008B";
            gameStateTitle.innerText = "TIEMPO EXTENDIDO";
            break;
    }
    if (timerState === timerStatus.STOPPED) {
        btnStopTimer.disabled = true;
        btnStartTimer.disabled = false;
        btnUpMinute.disabled = false;
        btnDownMinute.disabled = false;
        btnUpSecond.disabled = false;
        btnDownSecond.disabled = false;
        btnResetTimer.disabled = false;
        btnSetDefaultTimer.disabled = false;
        if(gameState ===  gameStatus.IN_PROGRESS)
            worker.postMessage("start-server-ping");
    }
    else if (timerState === timerStatus.RUNNING) {
        btnStopTimer.disabled = false;
        btnStartTimer.disabled = true;
        btnUpMinute.disabled = true;
        btnDownMinute.disabled = true;
        btnUpSecond.disabled = true;
        btnDownSecond.disabled = true;
        btnResetTimer.disabled = true;
        btnSetDefaultTimer.disabled = true;
    }
}