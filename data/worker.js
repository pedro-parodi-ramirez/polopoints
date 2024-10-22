const DOMAIN_worker = "polopoints.local"
const RESPONSE_TIMEOUT = 2000;
const PING_PERIOD = 5000;
let pingTimer = null;

// worker.js
self.addEventListener('message', async function (e) {
    switch(e.data) {
        case "start-server-ping":
            if(pingTimer === null){
                pingTimer = setInterval(async () => {
                    try{
                        const controller = new AbortController();
                        const timeoutId = setTimeout(() => controller.abort(), RESPONSE_TIMEOUT);
                        await fetch(`http://${DOMAIN_worker}/ping`, { signal: controller.signal });
                        self.postMessage("connected");
                        clearInterval(timeoutId);
                    }
                    catch(error){
                        self.postMessage("disconnected");
                    }
                }, PING_PERIOD);
            }
            break;
        case "stop-server-ping":
            if(pingTimer !== null){
                clearInterval(pingTimer);
                pingTimer = null;
            }
            break;
        default:
            if(pingTimer !== null){
                clearInterval(pingTimer);
                pingTimer = null;
            }
            break;
    }
})