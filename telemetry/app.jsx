// # Code for local dashboard, run using
// # npm create vite@latest speedo-app -- --template react
// # npm install
// # replace the app.jsx in the src file with this file
// # npm run dev


import { useState, useEffect, useRef, useCallback } from "react";


//​​ Haversine distance in meters between two lat/lng points
function haversine(lat1, lon1, lat2, lon2) {
 const R = 6371000;
 const toRad = (d) => (d * Math.PI) / 180;
 const dLat = toRad(lat2 - lat1);
 const dLon = toRad(lon2 - lon1);
 const a =
   Math.sin(dLat / 2) ** 2 +
   Math.cos(toRad(lat1)) * Math.cos(toRad(lat2)) * Math.sin(dLon / 2) ** 2;
 return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
}


// Smoothing filter — exponential moving average
function ema(prev, curr, alpha = 0.3) {
 return prev === null ? curr : alpha * curr + (1 - alpha) * prev;
}


const MAX_SPEED_MPH = 45;
const ARC_START = 135; // degrees from 12 o'clock, clockwise
const ARC_END = 405;
const ARC_RANGE = ARC_END - ARC_START;


function polarToCart(cx, cy, r, angleDeg) {
 const rad = ((angleDeg - 90) * Math.PI) / 180;
 return { x: cx + r * Math.cos(rad), y: cy + r * Math.sin(rad) };
}


function describeArc(cx, cy, r, startAngle, endAngle) {
 const s = polarToCart(cx, cy, r, startAngle);
 const e = polarToCart(cx, cy, r, endAngle);
 const large = endAngle - startAngle > 180 ? 1 : 0;
 return `M ${s.x} ${s.y} A ${r} ${r} 0 ${large} 1 ${e.x} ${e.y}`;
}


function SpeedGauge({ speed, maxSpeed = MAX_SPEED_MPH, unit = "mph" }) {
 const clamped = Math.min(Math.max(speed, 0), maxSpeed);
 const ratio = clamped / maxSpeed;
 const needleAngle = ARC_START + ratio * ARC_RANGE;


 const cx = 200, cy = 200, outerR = 170, innerR = 140, tickR = 155;
 const majorTicks = 10; // 0,5,10,...,45
 const minorPerMajor = 5;


 const ticks = [];
 for (let i = 0; i < majorTicks; i++) {
   const val = (maxSpeed / (majorTicks - 1)) * i;
   const ang = ARC_START + (i / (majorTicks - 1)) * ARC_RANGE;
   const outer = polarToCart(cx, cy, outerR, ang);
   const inner = polarToCart(cx, cy, innerR - 4, ang);
   const label = polarToCart(cx, cy, innerR - 22, ang);
   const dangerZone = val >= 35;
   ticks.push(
     <g key={`major-${i}`}>
       <line
         x1={outer.x} y1={outer.y} x2={inner.x} y2={inner.y}
         stroke={dangerZone ? "#ff3b30" : "#e0ddd5"}
         strokeWidth={2.5}
         strokeLinecap="round"
       />
       <text
         x={label.x} y={label.y}
         fill={dangerZone ? "#ff3b30" : "#c8c4b8"}
         fontSize="13"
         fontFamily="'JetBrains Mono', 'SF Mono', 'Fira Code', monospace"
         fontWeight="600"
         textAnchor="middle"
         dominantBaseline="central"
       >
         {Math.round(val)}
       </text>
     </g>
   );
   // minor ticks
   if (i < majorTicks - 1) {
     for (let m = 1; m < minorPerMajor; m++) {
       const mAng = ang + (m / minorPerMajor) * (ARC_RANGE / (majorTicks - 1));
       const mo = polarToCart(cx, cy, outerR, mAng);
       const mi = polarToCart(cx, cy, tickR, mAng);
       const mVal = val + (maxSpeed / (majorTicks - 1)) * (m / minorPerMajor);
       ticks.push(
         <line
           key={`minor-${i}-${m}`}
           x1={mo.x} y1={mo.y} x2={mi.x} y2={mi.y}
           stroke={mVal >= 35 ? "#ff3b3066" : "#e0ddd533"}
           strokeWidth={1}
           strokeLinecap="round"
         />
       );
     }
   }
 }


 const needleTip = polarToCart(cx, cy, outerR - 8, needleAngle);
 const needleBase1 = polarToCart(cx, cy, 8, needleAngle - 90);
 const needleBase2 = polarToCart(cx, cy, 8, needleAngle + 90);
 const needleTail = polarToCart(cx, cy, 30, needleAngle + 180);


 // Glow color based on speed
 const glowColor = clamped >= 35 ? "#ff3b30" : clamped >= 20 ? "#ff9500" : "#34c759";


 return (
   <svg viewBox="0 0 400 400" style={{ width: "100%", maxWidth: 420 }}>
     <defs>
       <radialGradient id="bgGrad" cx="50%" cy="50%" r="50%">
         <stop offset="0%" stopColor="#1a1a1e" />
         <stop offset="100%" stopColor="#0d0d0f" />
       </radialGradient>
       <filter id="needleGlow">
         <feGaussianBlur stdDeviation="4" result="blur" />
         <feFlood floodColor={glowColor} floodOpacity="0.7" />
         <feComposite in2="blur" operator="in" />
         <feMerge>
           <feMergeNode />
           <feMergeNode in="SourceGraphic" />
         </feMerge>
       </filter>
       <filter id="arcGlow">
         <feGaussianBlur stdDeviation="3" />
         <feComposite in="SourceGraphic" />
       </filter>
       <linearGradient id="arcFill" gradientUnits="userSpaceOnUse"
         x1={polarToCart(cx, cy, outerR, ARC_START).x}
         y1={polarToCart(cx, cy, outerR, ARC_START).y}
         x2={polarToCart(cx, cy, outerR, ARC_END).x}
         y2={polarToCart(cx, cy, outerR, ARC_END).y}
       >
         <stop offset="0%" stopColor="#34c759" />
         <stop offset="50%" stopColor="#ff9500" />
         <stop offset="100%" stopColor="#ff3b30" />
       </linearGradient>
     </defs>


     {/* Background */}
     <circle cx={cx} cy={cy} r={195} fill="url(#bgGrad)" stroke="#2a2a2e" strokeWidth="2" />


     {/* Outer ring */}
     <circle cx={cx} cy={cy} r={188} fill="none" stroke="#222226" strokeWidth="1" />


     {/* Track arc (background) */}
     <path
       d={describeArc(cx, cy, outerR, ARC_START, ARC_END)}
       fill="none" stroke="#2a2a2e" strokeWidth="8" strokeLinecap="round"
     />


     {/* Filled arc up to current speed */}
     {clamped > 0.5 && (
       <path
         d={describeArc(cx, cy, outerR, ARC_START, needleAngle)}
         fill="none" stroke="url(#arcFill)" strokeWidth="8" strokeLinecap="round"
         filter="url(#arcGlow)"
         style={{ transition: "d 0.3s ease-out" }}
       />
     )}


     {/* Ticks and labels */}
     {ticks}


     {/* Center hub */}
     <circle cx={cx} cy={cy} r="18" fill="#2a2a2e" stroke="#3a3a3e" strokeWidth="1" />
     <circle cx={cx} cy={cy} r="6" fill={glowColor} opacity="0.8" />


     {/* Needle */}
     <polygon
       points={`${needleTip.x},${needleTip.y} ${needleBase1.x},${needleBase1.y} ${needleTail.x},${needleTail.y} ${needleBase2.x},${needleBase2.y}`}
       fill={glowColor}
       filter="url(#needleGlow)"
       style={{ transition: "all 0.25s ease-out" }}
     />


     {/* Digital readout */}
     <text
       x={cx} y={cy + 55}
       fill={glowColor}
       fontSize="42"
       fontFamily="'JetBrains Mono', 'SF Mono', 'Fira Code', monospace"
       fontWeight="700"
       textAnchor="middle"
       dominantBaseline="central"
       style={{ filter: `drop-shadow(0 0 8px ${glowColor}66)` }}
     >
       {clamped.toFixed(1)}
     </text>
     <text
       x={cx} y={cy + 80}
       fill="#666"
       fontSize="14"
       fontFamily="'JetBrains Mono', 'SF Mono', 'Fira Code', monospace"
       fontWeight="500"
       textAnchor="middle"
       letterSpacing="3"
     >
       {unit.toUpperCase()}
     </text>
   </svg>
 );
}


export default function Speedometer() {
 const [speed, setSpeed] = useState(0);
 const [status, setStatus] = useState("idle"); // idle | listening | simulating | error
 const [unit, setUnit] = useState("mph"); // mph | km/h | m/s | knots
 const [log, setLog] = useState([]);
 const lastCoord = useRef(null);
 const lastTime = useRef(null);
 const smoothSpeed = useRef(null);
 const wsRef = useRef(null);
 const simRef = useRef(null);


 const unitConvert = useCallback((mps) => {
   switch (unit) {
     case "mph": return mps * 2.23694;
     case "km/h": return mps * 3.6;
     case "knots": return mps * 1.94384;
     default: return mps;
   }
 }, [unit]);


 const maxForUnit = unit === "mph" ? 45 : unit === "km/h" ? 75 : unit === "knots" ? 40 : 20;


 const processCoord = useCallback((lat, lon, timestamp) => {
   const now = timestamp || Date.now();
   if (lastCoord.current && lastTime.current) {
     const dt = (now - lastTime.current) / 1000;
     if (dt > 0 && dt < 10) {
       const dist = haversine(lastCoord.current.lat, lastCoord.current.lon, lat, lon);
       const rawMps = dist / dt;
       // Filter out GPS jitter — if standing still, noise ≈ 0-2 m/s
       const filtered = rawMps < 0.3 ? 0 : rawMps;
       const smoothed = ema(smoothSpeed.current, filtered, 0.35);
       smoothSpeed.current = smoothed;
       const display = unitConvert(smoothed);
       setSpeed(display);
       setLog((prev) => [
         { t: new Date(now).toLocaleTimeString(), lat, lon, spd: display.toFixed(1) },
         ...prev.slice(0, 19),
       ]);
     }
   }
   lastCoord.current = { lat, lon };
   lastTime.current = now;
 }, [unitConvert]);


 // --- WebSocket listener ---
 const startWS = useCallback((url) => {
   if (wsRef.current) wsRef.current.close();
   try {
     const ws = new WebSocket(url);
     wsRef.current = ws;
     setStatus("listening");
     ws.onmessage = (e) => {
       try {
         const d = JSON.parse(e.data);
         const lat = d.lat ?? d.latitude;
         const lon = d.lon ?? d.lng ?? d.longitude;
         if (lat != null && lon != null) processCoord(lat, lon, d.timestamp);
       } catch {}
     };
     ws.onerror = () => { setStatus("error"); };
     ws.onclose = () => { setStatus("idle"); wsRef.current = null; };
   } catch { setStatus("error"); }
 }, [processCoord]);


 // --- Browser Geolocation (for testing) ---
 const geoWatchRef = useRef(null);
 const startGeo = useCallback(() => {
   if (!navigator.geolocation) { setStatus("error"); return; }
   setStatus("listening");
   geoWatchRef.current = navigator.geolocation.watchPosition(
     (pos) => processCoord(pos.coords.latitude, pos.coords.longitude, pos.timestamp || Date.now()),
     () => setStatus("error"),
     { enableHighAccuracy: true, maximumAge: 0 }
   );
 }, [processCoord]);


 // --- Simulation mode ---
 const startSim = useCallback(() => {
   setStatus("simulating");
   const baseLat = 34.0689, baseLon = -118.4452;
   let simSpeedMph = 0;
   let tick = 0;


   // Each segment: [targetMph, durationTicks] — Baja car on a course
   const segments = [
     [10, 4], [25, 5], [35, 5], [20, 4], [30, 5],
     [40, 4], [25, 4], [15, 3], [35, 5], [10, 3], [0, 3],
   ];
   let segIdx = 0, segTick = 0;


   simRef.current = setInterval(() => {
     const [target] = segments[segIdx % segments.length];
     simSpeedMph += (target - simSpeedMph) * 0.15;


     const mps = simSpeedMph / 2.23694;
     const noisy = mps + (Math.random() - 0.5) * 0.6;
     const filtered = noisy < 0.3 ? 0 : noisy;
     const smoothed = ema(smoothSpeed.current, filtered, 0.35);
     smoothSpeed.current = smoothed;
     const display = unitConvert(smoothed);
     setSpeed(display);


     const heading = tick * 0.02;
     const simLat = baseLat + Math.cos(heading) * tick * 0.00001;
     const simLon = baseLon + Math.sin(heading) * tick * 0.00001;


     setLog((prev) => [
       { t: new Date().toLocaleTimeString(), lat: simLat.toFixed(6), lon: simLon.toFixed(6), spd: display.toFixed(1) },
       ...prev.slice(0, 19),
     ]);


     tick++;
     segTick++;
     if (segTick >= segments[segIdx % segments.length][1]) {
       segIdx++;
       segTick = 0;
     }
   }, 1000);
 }, [unitConvert]);


 const stop = useCallback(() => {
   if (wsRef.current) wsRef.current.close();
   if (geoWatchRef.current) navigator.geolocation.clearWatch(geoWatchRef.current);
   if (simRef.current) clearInterval(simRef.current);
   wsRef.current = null;
   geoWatchRef.current = null;
   simRef.current = null;
   setStatus("idle");
   smoothSpeed.current = null;
   lastCoord.current = null;
   lastTime.current = null;
 }, []);


 useEffect(() => () => stop(), [stop]);


 const [wsUrl, setWsUrl] = useState("ws://localhost:8765");


 const statusColor = { idle: "#666", listening: "#34c759", simulating: "#ff9500", error: "#ff3b30" };


 return (
   <div style={{
     minHeight: "100vh",
     background: "#0d0d0f",
     color: "#e0ddd5",
     fontFamily: "'JetBrains Mono', 'SF Mono', 'Fira Code', monospace",
     display: "flex",
     flexDirection: "column",
     alignItems: "center",
     padding: "24px 16px",
     gap: 16,
   }}>
     {/* Header */}
     <div style={{ display: "flex", alignItems: "center", gap: 10, marginBottom: 4 }}>
       <div style={{
         width: 8, height: 8, borderRadius: "50%",
         background: statusColor[status],
         boxShadow: `0 0 8px ${statusColor[status]}`,
       }} />
       <span style={{ fontSize: 11, color: "#888", letterSpacing: 2, textTransform: "uppercase" }}>
         {status === "idle" ? "standby" : status === "listening" ? "live" : status === "simulating" ? "demo" : "error"}
       </span>
     </div>


     {/* Gauge */}
     <SpeedGauge speed={speed} maxSpeed={maxForUnit} unit={unit} />


     {/* Unit selector */}
     <div style={{ display: "flex", gap: 4, background: "#1a1a1e", borderRadius: 8, padding: 3 }}>
       {["mph", "km/h", "m/s", "knots"].map((u) => (
         <button
           key={u}
           onClick={() => setUnit(u)}
           style={{
             padding: "6px 14px",
             borderRadius: 6,
             border: "none",
             background: unit === u ? "#2a2a2e" : "transparent",
             color: unit === u ? "#e0ddd5" : "#666",
             fontSize: 11,
             fontFamily: "inherit",
             cursor: "pointer",
             fontWeight: unit === u ? 700 : 400,
             letterSpacing: 1,
             transition: "all 0.15s",
           }}
         >
           {u}
         </button>
       ))}
     </div>


     {/* Controls */}
     <div style={{
       display: "flex", flexDirection: "column", gap: 8,
       width: "100%", maxWidth: 400, marginTop: 8,
     }}>
       {status === "idle" ? (
         <>
           <div style={{ display: "flex", gap: 6 }}>
             <input
               value={wsUrl}
               onChange={(e) => setWsUrl(e.target.value)}
               placeholder="ws://host:port"
               style={{
                 flex: 1, padding: "10px 12px", borderRadius: 8,
                 border: "1px solid #2a2a2e", background: "#1a1a1e",
                 color: "#e0ddd5", fontSize: 12, fontFamily: "inherit",
                 outline: "none",
               }}
             />
             <button onClick={() => startWS(wsUrl)} style={btnStyle("#34c759")}>
               Connect
             </button>
           </div>
           <div style={{ display: "flex", gap: 6 }}>
             <button onClick={startGeo} style={{ ...btnStyle("#007aff"), flex: 1 }}>
               Use GPS
             </button>
             <button onClick={startSim} style={{ ...btnStyle("#ff9500"), flex: 1 }}>
               Simulate
             </button>
           </div>
         </>
       ) : (
         <button onClick={stop} style={btnStyle("#ff3b30")}>
           Stop
         </button>
       )}
     </div>


     {/* Data log */}
     {log.length > 0 && (
       <div style={{
         width: "100%", maxWidth: 400, marginTop: 12,
         background: "#1a1a1e", borderRadius: 10, padding: 12,
         maxHeight: 180, overflowY: "auto",
       }}>
         <div style={{ fontSize: 10, color: "#555", letterSpacing: 2, marginBottom: 8, textTransform: "uppercase" }}>
           Recent Readings
         </div>
         {log.map((l, i) => (
           <div key={i} style={{
             display: "flex", justifyContent: "space-between",
             fontSize: 11, color: i === 0 ? "#e0ddd5" : "#555",
             padding: "3px 0", borderBottom: "1px solid #222226",
           }}>
             <span>{l.t}</span>
             <span style={{ color: i === 0 ? "#34c759" : "#555" }}>{l.spd} {unit}</span>
           </div>
         ))}
       </div>
     )}


     {/* Integration hint */}
     {status === "idle" && (
       <div style={{
         width: "100%", maxWidth: 400, marginTop: 8,
         background: "#1a1a1e", borderRadius: 10, padding: 14,
         fontSize: 11, color: "#555", lineHeight: 1.6,
       }}>
         <div style={{ color: "#888", fontWeight: 700, marginBottom: 6, letterSpacing: 1, fontSize: 10 }}>
           INTEGRATION
         </div>
         <span style={{ color: "#888" }}>WebSocket JSON format:</span>
         <pre style={{
           margin: "6px 0 0", padding: 8, background: "#0d0d0f",
           borderRadius: 6, fontSize: 10, color: "#34c759", overflow: "auto",
         }}>
{`{ "lat": 34.0689, "lon": -118.4452 }`}
         </pre>
       </div>
     )}
   </div>
 );
}


const btnStyle = (color) => ({
 padding: "10px 18px",
 borderRadius: 8,
 border: "none",
 background: color + "22",
 color: color,
 fontSize: 12,
 fontFamily: "'JetBrains Mono', 'SF Mono', monospace",
 fontWeight: 600,
 cursor: "pointer",
 letterSpacing: 1,
 transition: "all 0.15s",
});
