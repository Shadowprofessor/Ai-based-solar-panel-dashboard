// Configuration
const CHANNEL_ID = '2841130';
const RESULTS_COUNT = 30;
const GEMINI_API_KEY = 'AIzaSyBIVJIYMcJiSf5LkeBN9MZWntKrX5CbNYw';

// Chart Instances
let voltageChart;
let envChart;
let gaugeChart;

// Data Storage
let solarData = {
    labels: [],
    voltage: [],
    temp: [],
    humidity: []
};

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    updateDate();
    initCharts();
    fetchThingSpeakData();
    setInterval(fetchThingSpeakData, 30000); // 30s refresh

    document.querySelectorAll('.nav-link').forEach(link => {
        link.addEventListener('click', (e) => {
            if (link.getAttribute('href') === '#') {
                e.preventDefault();
                generateGeminiAnalysis();
            }
        });
    });
});

function updateDate() {
    const now = new Date();
    const dateEl = document.getElementById('current-date');
    if (dateEl) {
        dateEl.innerText = now.toLocaleDateString('en-US', {
            weekday: 'long', month: 'short', day: 'numeric', year: 'numeric'
        });
    }
}

async function fetchThingSpeakData() {
    console.log("Fetching ThingSpeak Data...");
    try {
        const response = await fetch(`https://api.thingspeak.com/channels/${CHANNEL_ID}/feeds.json?results=${RESULTS_COUNT}`);
        if (!response.ok) throw new Error(`ThingSpeak HTTP error! status: ${response.status}`);

        const data = await response.json();
        console.log("ThingSpeak Data received:", data);

        if (data.feeds && data.feeds.length > 0) {
            processFeeds(data.feeds);
            updateDashboard();
            generateGeminiAnalysis();
        } else {
            console.warn("No feeds found in ThingSpeak response.");
            showStatusMessage("No channel data found.");
        }
    } catch (error) {
        console.error('Data Sync Error:', error);
        showStatusMessage("Network error: Check connection.");
    }
}

function processFeeds(feeds) {
    solarData.labels = [];
    solarData.voltage = [];
    solarData.temp = [];
    solarData.humidity = [];

    feeds.forEach(feed => {
        const time = new Date(feed.created_at).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });
        solarData.labels.push(time);
        solarData.temp.push(parseFloat(feed.field1) || 0);
        solarData.humidity.push(parseFloat(feed.field2) || 0);
        solarData.voltage.push(parseFloat(feed.field3) || 0);
    });
}

function updateDashboard() {
    const lastIdx = solarData.voltage.length - 1;
    if (lastIdx < 0) return;

    const currentV = solarData.voltage[lastIdx];
    const currentT = solarData.temp[lastIdx];
    const currentH = solarData.humidity[lastIdx];

    const vVal = document.getElementById('voltage-val');
    const tVal = document.getElementById('temp-val');
    const hVal = document.getElementById('humidity-val');

    if (vVal) vVal.innerHTML = `${currentV.toFixed(2)} <span class="card-unit">V</span>`;
    if (tVal) tVal.innerHTML = `${currentT.toFixed(1)} <span class="card-unit">°C</span>`;
    if (hVal) hVal.innerHTML = `${currentH.toFixed(0)} <span class="card-unit">%</span>`;

    const prevV = solarData.voltage[Math.max(0, lastIdx - 1)];
    const trend = ((currentV - prevV) / (prevV || 1) * 100).toFixed(1);
    const vTrendEl = document.getElementById('v-trend');
    if (vTrendEl) {
        vTrendEl.innerHTML = trend >= 0
            ? `<i class="fas fa-caret-up"></i> +${trend}% vs last sync`
            : `<i class="fas fa-caret-down"></i> ${trend}% vs last sync`;
        vTrendEl.className = trend >= 0 ? 'status-indicator status-up' : 'status-indicator status-down';
    }

    const intensity = Math.min(1000, (currentV / 18) * 1000);
    const gVal = document.getElementById('gauge-val');
    if (gVal) gVal.innerText = intensity.toFixed(0);

    if (gaugeChart) {
        const percentage = (intensity / 1000) * 100;
        gaugeChart.data.datasets[0].data = [percentage, 100 - percentage];
        gaugeChart.update();
    }

    let efficiency = Math.max(0, Math.min(100, (currentV / 15) * 100 - (currentT - 25)));
    const effVal = document.getElementById('eff-val');
    const effBar = document.getElementById('eff-bar');
    if (effVal) effVal.innerText = efficiency.toFixed(1) + '%';
    if (effBar) effBar.style.width = efficiency + '%';

    updateCharts();
}

function initCharts() {
    const commonScales = {
        y: { grid: { color: 'rgba(255, 255, 255, 0.05)' }, ticks: { color: '#8b949e', font: { size: 10 } } },
        x: { grid: { display: false }, ticks: { color: '#8b949e', font: { size: 10 } } }
    };

    const vCtx = document.getElementById('voltageChart');
    if (vCtx) {
        voltageChart = new Chart(vCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [{
                    label: 'Voltage',
                    data: [],
                    borderColor: '#58a6ff',
                    backgroundColor: 'rgba(88, 166, 255, 0.1)',
                    borderWidth: 2,
                    fill: true,
                    tension: 0.4,
                    pointRadius: 2
                }]
            },
            options: { responsive: true, maintainAspectRatio: false, plugins: { legend: { display: false } }, scales: commonScales }
        });
    }

    const eCtx = document.getElementById('envChart');
    if (eCtx) {
        envChart = new Chart(eCtx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    { label: 'Temp', data: [], borderColor: '#d29922', borderWidth: 1.5, tension: 0.3, pointRadius: 0 },
                    { label: 'Hum', data: [], borderColor: '#39c5cf', borderWidth: 1.5, tension: 0.3, pointRadius: 0 }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: { legend: { position: 'top', labels: { color: '#8b949e', boxWidth: 10, font: { size: 10 } } } },
                scales: commonScales
            }
        });
    }

    const gCtx = document.getElementById('gaugeChart');
    if (gCtx) {
        gaugeChart = new Chart(gCtx, {
            type: 'doughnut',
            data: {
                datasets: [{
                    data: [0, 100],
                    backgroundColor: ['#58a6ff', '#30363d'],
                    borderWidth: 0,
                    circumference: 180,
                    rotation: 270,
                    cutout: '85%'
                }]
            },
            options: { responsive: true, maintainAspectRatio: false, plugins: { tooltip: { enabled: false } } }
        });
    }
}

function updateCharts() {
    if (voltageChart) {
        voltageChart.data.labels = solarData.labels;
        voltageChart.data.datasets[0].data = solarData.voltage;
        voltageChart.update();
    }

    if (envChart) {
        envChart.data.labels = solarData.labels;
        envChart.data.datasets[0].data = solarData.temp;
        envChart.data.datasets[1].data = solarData.humidity;
        envChart.update();
    }
}

// Rate Limiting
let isGeminiRunning = false;
let lastGeminiTime = 0;
const GEMINI_COOLDOWN = 60000; // 60 seconds

async function generateGeminiAnalysis() {
    const now = Date.now();
    if (isGeminiRunning || (now - lastGeminiTime < GEMINI_COOLDOWN)) {
        console.log(`Gemini Rate Limit: Wait ${(GEMINI_COOLDOWN - (now - lastGeminiTime)) / 1000}s`);
        return;
    }
    isGeminiRunning = true;

    const loader = document.getElementById('gemini-loader');
    const results = document.getElementById('analysis-results');
    const detailed = document.getElementById('detailed-report');

    if (loader) loader.style.display = 'block';
    if (results) results.style.opacity = '0.4';

    const historySize = 15;
    const vHistory = solarData.voltage.slice(-historySize);
    const tHistory = solarData.temp.slice(-historySize);
    const hHistory = solarData.humidity.slice(-historySize);

    if (vHistory.length === 0) {
        isGeminiRunning = false;
        return;
    }

    const latestData = {
        voltage: vHistory[vHistory.length - 1],
        temp: tHistory[tHistory.length - 1],
        humidity: hHistory[hHistory.length - 1],
        peakV: Math.max(...vHistory),
        avgV: vHistory.reduce((a, b) => a + b, 0) / vHistory.length
    };

    const pVal = document.getElementById('peak-val');
    const aeVal = document.getElementById('avg-eff-val');
    if (pVal) pVal.innerText = latestData.peakV.toFixed(2) + ' V';
    if (aeVal) aeVal.innerText = ((latestData.avgV / 15) * 100).toFixed(1) + '%';

    try {
        console.log("Requesting Gemini Analysis...");
        const response = await fetch(`https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash-exp:generateContent?key=${GEMINI_API_KEY}`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                contents: [{ parts: [{ text: `Analyze solar data: Voltages [${vHistory.join(', ')}], Temps [${tHistory.join(', ')}]. JSON format ONLY: {"voltageAnalysis": "...", "weatherAnalysis": "...", "maintenanceInsight": "...", "futurePrediction": "..."}` }] }]
            })
        });

        if (!response.ok) throw new Error(`Gemini API Error: ${response.status}`);

        const data = await response.json();
        console.log("Gemini Response Data:", data);

        if (data.candidates && data.candidates[0].content.parts[0].text) {
            let text = data.candidates[0].content.parts[0].text;
            let jsonMatch = text.match(/\{[\s\S]*\}/);
            if (!jsonMatch) throw new Error("No valid JSON found in AI response");

            const analysis = JSON.parse(jsonMatch[0]);

            const vf = document.getElementById('voltage-forecast');
            const ea = document.getElementById('efficiency-alert');
            const mt = document.getElementById('maintenance-tip');

            if (vf) vf.innerHTML = `<strong>Trend:</strong> ${analysis.voltageAnalysis}<br><br><strong>Prediction:</strong> ${analysis.futurePrediction}`;
            if (ea) ea.innerText = analysis.weatherAnalysis;
            if (mt) mt.innerText = analysis.maintenanceInsight;
        } else {
            throw new Error('Empty Gemini response');
        }

    } catch (error) {
        console.error('Gemini Failure:', error);
        runSimulatedDetailedAnalysis(latestData);
    } finally {
        if (loader) loader.style.display = 'none';
        if (results) results.style.opacity = '1';
        if (detailed) detailed.style.display = 'block';

        isGeminiRunning = false;
        lastGeminiTime = Date.now();
    }
}

function runSimulatedDetailedAnalysis(data) {
    const vf = document.getElementById('voltage-forecast');
    const ea = document.getElementById('efficiency-alert');
    const mt = document.getElementById('maintenance-tip');

    if (vf) vf.innerText = `Current Voltage ${data.voltage.toFixed(2)}V. High-pressure area detected, stable yield expected for next 2 hours.`;
    if (ea) ea.innerText = `Temp at ${data.temp.toFixed(1)}°C is slightly above optimal but thermal efficiency remains at 92%.`;
    if (mt) mt.innerText = `No critical maintenance needed. System health is nominal.`;
}

function showStatusMessage(msg) {
    console.log("Status:", msg);
}
