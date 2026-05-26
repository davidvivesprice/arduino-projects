#pragma once
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "config.h"
#include "room.h"
#include "speaker.h"
#include "discovery.h"
#include "actions.h"
#include "modes.h"
#include "logo.h"
#include "updater.h"

static WebServer web(80);
static char deviceHostname[24];
static String deviceName;

// Defined in encoder.h — webui.h reads it for the status endpoint without
// pulling the full encoder header (encoder.h includes webui.h transitively).
extern volatile unsigned long lastActivityMs;
extern bool encoderInvert;
extern int  volumeStep;
extern String        lastFiredGid;
extern bool          lastFiredOk;
extern unsigned long lastFiredMs;
void saveEncoderInvert(bool v);  // implemented in encoder.h
void saveVolumeStep(int v);      // implemented in encoder.h

// Ring buffer log for web UI
static const int LOG_LINES = 40;
static String logRing[LOG_LINES];
static int logHead = 0;

void logEvent(const char* fmt, ...) {
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.println(buf);
  // Timestamp + message
  char entry[160];
  snprintf(entry, sizeof(entry), "%lu %s", millis() / 1000, buf);
  logRing[logHead] = entry;
  logHead = (logHead + 1) % LOG_LINES;
}

// --- Handlers ---

static void serveRoot() {
  static const char HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head>
<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Sonos Remote</title>
<link rel='preconnect' href='https://fonts.googleapis.com'>
<link rel='preconnect' href='https://fonts.gstatic.com' crossorigin>
<link href='https://fonts.googleapis.com/css2?family=Fraunces:opsz,wght@9..144,300;9..144,500;9..144,600&family=IBM+Plex+Sans:wght@300;400;500;600&family=JetBrains+Mono:wght@400;500;600&display=swap' rel='stylesheet'>
<style>
:root{
  --bg:#0a0a0c;
  --surface:#121218;
  --surface-2:#16161e;
  --rail:#1c1c26;
  --ink:#e9e7e3;
  --ink-2:#a09c95;
  --ink-3:#5a564f;
  --ink-4:#2e2c28;
  --accent:#7B2D2D;
  --accent-2:#9a3838;
  --accent-soft:#3a1418;
  --serif:'Fraunces',Georgia,serif;
  --sans:'IBM Plex Sans',system-ui,sans-serif;
  --mono:'JetBrains Mono',ui-monospace,monospace;
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--bg);color:var(--ink)}
body{
  font-family:var(--sans);font-weight:400;font-size:14px;line-height:1.55;
  max-width:1040px;margin:0 auto;padding:28px 24px 40px;
  background:
    radial-gradient(900px 600px at 50% -10%, #14141c 0%, transparent 60%),
    linear-gradient(180deg, #0a0a0c 0%, #08080a 100%);
  min-height:100vh;
  -webkit-font-smoothing:antialiased;text-rendering:optimizeLegibility;
}
::selection{background:var(--accent);color:#fff}
.masthead{display:flex;flex-direction:column;align-items:center;gap:6px;padding:8px 0 18px;border-bottom:1px solid var(--ink-4);margin-bottom:24px}
.logo img{height:56px;opacity:.92;filter:saturate(.85)}
.eyebrow{font-family:var(--mono);font-size:10px;font-weight:500;letter-spacing:4px;text-transform:uppercase;color:var(--ink-3);margin-top:2px}
.room{font-family:var(--serif);font-size:22px;font-weight:400;color:var(--ink);letter-spacing:.3px;margin-top:4px;text-align:center;line-height:1.1;display:flex;align-items:center;justify-content:center;gap:10px}
.room.unassigned{font-style:italic;color:#a55;font-size:16px;letter-spacing:1px;text-transform:uppercase;font-family:var(--mono)}
.room .roomhost{display:block;font-family:var(--mono);font-size:10px;color:var(--ink-3);letter-spacing:2px;margin-top:4px;text-transform:lowercase;font-style:normal}
.room .actdot{display:inline-block;width:10px;height:10px;border-radius:50%;background:#2a2a2e;transition:background .1s,box-shadow .1s,transform .1s;flex-shrink:0}
.room .actdot.live{background:#5dd17b;box-shadow:0 0 14px #5dd17b,0 0 4px #5dd17b;transform:scale(1.15)}
/* Gesture row flash — green = SOAP success, red = Sonos rejected. */
@keyframes flashOk { 0% { background: rgba(93,209,123,.45); box-shadow: inset 0 0 24px rgba(93,209,123,.4); } 100% { background: transparent; box-shadow: none; } }
@keyframes flashFail { 0% { background: rgba(255,90,90,.45); box-shadow: inset 0 0 24px rgba(255,90,90,.4); } 100% { background: transparent; box-shadow: none; } }
.gesture-row.flash-ok   { animation: flashOk 1.6s ease-out; }
.gesture-row.flash-fail { animation: flashFail 1.6s ease-out; }
.room .roomtxt{display:inline-block}
.dev{font-family:var(--mono);font-size:11px;color:var(--ink-3);cursor:pointer;text-align:center;letter-spacing:.5px;transition:color .2s}
.steprow{font-family:var(--mono);font-size:10px;color:var(--ink-3);text-align:center;letter-spacing:1px;text-transform:uppercase;margin-top:8px;display:flex;align-items:center;justify-content:center;gap:10px}
.steprow input[type=range]{width:140px;height:14px;-webkit-appearance:none;background:transparent}
.steprow input[type=range]::-webkit-slider-runnable-track{height:3px;background:var(--ink-4);border-radius:2px}
.steprow input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:12px;height:12px;border-radius:50%;background:var(--accent);margin-top:-4px;cursor:pointer}
.steprow .stepval{display:inline-block;width:18px;text-align:right;color:var(--ink);font-variant-numeric:tabular-nums}
.invrow{font-family:var(--mono);font-size:10px;color:var(--ink-3);text-align:center;letter-spacing:1px;text-transform:uppercase;margin-top:6px;display:flex;align-items:center;justify-content:center;gap:8px}
.invrow .toggle{position:relative;width:32px;height:18px;background:var(--ink-4);border-radius:9px;cursor:pointer;transition:background .2s;border:1px solid var(--ink-4)}
.invrow .toggle.on{background:var(--accent);border-color:var(--accent)}
.invrow .toggle::after{content:'';position:absolute;top:2px;left:2px;width:12px;height:12px;background:var(--ink-3);border-radius:50%;transition:all .2s}
.invrow .toggle.on::after{left:16px;background:#fff}
.dev:hover{color:var(--accent)}
.dev .dot{display:inline-block;width:6px;height:6px;border-radius:50%;background:var(--accent);margin-right:8px;vertical-align:middle;box-shadow:0 0 8px var(--accent)}
.col-narrow{max-width:480px;margin:0 auto}
.col-wide{max-width:980px;margin:24px auto 0}
.card{background:var(--surface);border:1px solid var(--ink-4);border-radius:12px;padding:18px;margin:14px 0;position:relative}
.gauge-wrap{display:flex;justify-content:center;align-items:center;padding:14px 0 6px}
.gauge{position:relative;width:188px;height:188px;cursor:ns-resize;touch-action:none}
.gauge svg{width:100%;height:100%}
.gauge .track{fill:none;stroke:var(--rail);stroke-width:8;stroke-linecap:round}
.gauge .fill{fill:none;stroke:var(--accent);stroke-width:8;stroke-linecap:round;transition:stroke-dashoffset .15s ease-out,stroke .2s;filter:drop-shadow(0 0 6px var(--accent-soft))}
.gauge .center{position:absolute;inset:0;display:flex;flex-direction:column;justify-content:center;align-items:center}
.gauge .num{font-family:var(--serif);font-size:64px;font-weight:300;color:var(--ink);letter-spacing:-3px;line-height:1;font-variant-numeric:tabular-nums;transition:color .2s;font-feature-settings:"ss01"}
.gauge .num.mut{color:#a55}
.gauge .label{font-family:var(--mono);font-size:9px;font-weight:500;color:var(--ink-3);margin-top:6px;letter-spacing:3px;text-transform:uppercase}
.modepill{font-family:var(--mono);display:inline-flex;align-items:center;gap:6px;padding:5px 11px;border-radius:2px;font-size:10px;font-weight:600;letter-spacing:2px;text-transform:uppercase;background:transparent;color:var(--ink-3);border:1px solid var(--ink-4);margin-top:10px;transition:all .25s}
.modepill .mp-dot{width:5px;height:5px;border-radius:50%;background:var(--ink-3)}
.modepill.bass{color:var(--accent);border-color:var(--accent)}
.modepill.bass .mp-dot{background:var(--accent);animation:pulseSlow 1s infinite}
.modepill.treble{color:var(--accent-2);border-color:var(--accent-2)}
.modepill.treble .mp-dot{background:var(--accent-2);animation:pulseFast .33s infinite}
@keyframes pulseSlow{50%{opacity:.25;transform:scale(.7)}}
@keyframes pulseFast{50%{opacity:.25;transform:scale(.7)}}
.meta{text-align:center;margin-top:6px}
.spkname{font-family:var(--serif);font-size:18px;font-weight:500;color:var(--ink);letter-spacing:-.2px}
.badges{margin-top:8px;display:flex;justify-content:center;gap:6px}
.badges span{font-family:var(--mono);padding:3px 8px;border-radius:2px;font-size:9px;font-weight:600;letter-spacing:1.5px;text-transform:uppercase}
.on{background:transparent;color:#7aa97a;border:1px solid #2c4a2c}
.off{background:transparent;color:var(--ink-3);border:1px solid var(--ink-4)}
.mut{background:transparent;color:#a55;border:1px solid #4a2828}
.spk{cursor:pointer;border:1px solid var(--ink-4);transition:all .15s;display:flex;justify-content:space-between;align-items:center}
.spk:hover{border-color:var(--accent);background:#15101a}
.spk.act{border-color:var(--accent);background:var(--accent-soft)}
.spk.act::before{content:'';display:inline-block;width:4px;height:24px;background:var(--accent);margin-right:12px;border-radius:2px}
.sn{font-family:var(--serif);font-size:15px;font-weight:500;letter-spacing:-.2px}
.si{font-family:var(--mono);color:var(--ink-3);font-size:11px}
.np{display:flex;align-items:center;gap:16px}
.np img{width:72px;height:72px;border-radius:4px;background:#0a0a0a;object-fit:cover;flex-shrink:0;border:1px solid var(--ink-4)}
.np .noart{width:72px;height:72px;border-radius:4px;background:#0a0a0a;flex-shrink:0;display:flex;align-items:center;justify-content:center;color:var(--ink-4);font-size:28px;border:1px solid var(--ink-4)}
.np .info{min-width:0;flex:1}
.np .t{font-family:var(--serif);font-size:17px;font-weight:500;color:var(--ink);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;letter-spacing:-.3px}
.np .a{font-size:13px;color:var(--ink-2);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;margin-top:2px}
.np .al{font-size:11px;color:var(--ink-3);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;margin-top:1px;font-style:italic}
.np .time{font-family:var(--mono);font-size:10px;color:var(--ink-3);margin-top:6px;letter-spacing:1px}
.transport{display:flex;justify-content:center;gap:18px;padding:8px 0 4px}
.tbtn{background:transparent;border:1px solid var(--ink-4);color:var(--ink-2);width:46px;height:46px;border-radius:50%;cursor:pointer;display:flex;align-items:center;justify-content:center;transition:all .15s;user-select:none}
.tbtn:hover{border-color:var(--accent);color:var(--ink);background:rgba(123,45,45,.08)}
.tbtn:active{transform:scale(.92)}
.tbtn svg{width:18px;height:18px;fill:currentColor}
.tbtn.play{background:var(--accent);border-color:var(--accent);color:#fff;width:56px;height:56px;box-shadow:0 4px 16px rgba(123,45,45,.3)}
.tbtn.play:hover{background:var(--accent-2);border-color:var(--accent-2)}
.tbtn.play svg{width:22px;height:22px}
.tbtn.muted{color:#c66;border-color:#c66}
.scan{font-family:var(--mono);font-size:11px;color:var(--ink-3);text-align:center;padding:8px;letter-spacing:.5px}
.btns{display:flex;gap:10px;flex-wrap:wrap}
.btn{flex:1;background:transparent;border:1px solid var(--ink-4);color:var(--ink-2);padding:9px 0;border-radius:4px;cursor:pointer;font:500 12px var(--mono);text-align:center;transition:all .15s;letter-spacing:1.5px;text-transform:uppercase}
.btn:hover{border-color:var(--accent);color:var(--ink)}
.log{background:#06060a;border:1px solid var(--ink-4);border-radius:6px;padding:12px 14px;margin-top:14px;font:11px/1.65 var(--mono);color:var(--ink-3);max-height:180px;overflow-y:auto;white-space:pre-wrap;scroll-behavior:smooth;letter-spacing:.3px}
.log::-webkit-scrollbar{width:6px}
.log::-webkit-scrollbar-thumb{background:var(--ink-4);border-radius:3px}
details.section{background:var(--surface);border:1px solid var(--ink-4);border-radius:8px;margin:14px 0;overflow:hidden}
details.section>summary{padding:14px 18px;cursor:pointer;font-family:var(--serif);font-size:14px;font-weight:500;color:var(--ink);letter-spacing:-.1px;list-style:none;display:flex;justify-content:space-between;align-items:center;transition:background .15s}
details.section>summary>span{font-family:var(--mono);font-size:10px;font-weight:400;letter-spacing:1.5px;text-transform:uppercase;color:var(--ink-3)}
details.section>summary::-webkit-details-marker{display:none}
details.section>summary::after{content:'+';color:var(--ink-3);font-size:18px;font-weight:300;transition:transform .25s;font-family:var(--mono)}
details.section[open]>summary::after{transform:rotate(45deg)}
details.section>summary:hover{background:#16161e}
.section-body{padding:4px 18px 20px;border-top:1px solid var(--ink-4)}

/* Side-by-side workspace */
.workspace{display:grid;grid-template-columns:1fr 1px 1fr;gap:0;background:var(--surface);border:1px solid var(--ink-4);border-radius:8px;overflow:hidden}
.workspace .rail{background:repeating-linear-gradient(to bottom, var(--ink-4) 0 4px, transparent 4px 12px)}
.ws-col{padding:18px 20px;min-width:0}
.ws-head{display:flex;align-items:baseline;justify-content:space-between;padding-bottom:14px;border-bottom:1px solid var(--ink-4);margin-bottom:14px}
.ws-head h3{font-family:var(--serif);font-size:15px;font-weight:500;letter-spacing:-.2px;color:var(--ink)}
.ws-head .sub{font-family:var(--mono);font-size:9px;letter-spacing:2px;text-transform:uppercase;color:var(--ink-3)}
@media(max-width:760px){
  .workspace{grid-template-columns:1fr;grid-template-rows:auto auto auto}
  .workspace .rail{height:1px;background:var(--ink-4)}
}

/* Filter chips for action library */
.filter-chips{display:flex;flex-wrap:wrap;gap:5px;margin-bottom:14px}
.chip{font-family:var(--mono);font-size:10px;font-weight:500;letter-spacing:1px;text-transform:uppercase;padding:5px 10px;border:1px solid var(--ink-4);border-radius:2px;color:var(--ink-3);cursor:pointer;transition:all .12s;background:transparent}
.chip:hover{color:var(--ink-2);border-color:var(--ink-3)}
.chip.active{color:var(--ink);border-color:var(--accent);background:var(--accent-soft)}

/* Action pills — single column for narrow workspace cells */
.actions-list{display:flex;flex-direction:column;gap:6px;max-height:520px;overflow-y:auto;padding-right:4px}
.actions-list::-webkit-scrollbar{width:4px}
.actions-list::-webkit-scrollbar-thumb{background:var(--ink-4);border-radius:2px}
.action-pill{background:#0e0e14;border:1px solid var(--ink-4);color:var(--ink-2);padding:9px 12px;border-radius:4px;font-size:12px;cursor:grab;user-select:none;display:flex;align-items:center;gap:10px;transition:all .15s;position:relative}
.action-pill .alabel{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:400;color:var(--ink)}
.action-pill .acat{font-family:var(--mono);font-size:9px;color:var(--ink-3);letter-spacing:1px;text-transform:uppercase;flex-shrink:0}
.action-pill:hover{border-color:var(--accent);background:#13101a;color:var(--ink)}
.action-pill:hover .acat{color:var(--accent)}
.action-pill:active{cursor:grabbing}
.action-pill.dragging{opacity:.35;transform:scale(.97)}
.action-pill .try{border:1px solid var(--ink-4);color:var(--ink-3);width:22px;height:22px;border-radius:50%;display:inline-flex;align-items:center;justify-content:center;font-size:9px;cursor:pointer;transition:all .15s;flex-shrink:0}
.action-pill .try:hover{background:var(--accent);color:#fff;border-color:var(--accent)}
.action-pill input{width:48px;background:#06060a;border:1px solid var(--ink-4);color:var(--ink);border-radius:2px;font-size:11px;padding:2px 4px;text-align:center;font-family:var(--mono)}

/* Gesture rows */
.gesture-list{display:flex;flex-direction:column;gap:8px}
.gesture-row{display:grid;grid-template-columns:78px 1fr auto;align-items:center;gap:10px;padding:6px 0}
.gesture-row .gname{font-family:var(--mono);font-size:11px;color:var(--ink-2);font-weight:500;letter-spacing:.5px;border-right:1px solid var(--ink-4);padding-right:10px;line-height:1.2}
.gesture-row .gname small{display:block;font-size:9px;color:var(--ink-3);margin-top:2px;letter-spacing:.3px}
.gesture-row .test{background:transparent;border:1px solid var(--ink-4);color:var(--ink-3);padding:5px 10px;border-radius:2px;cursor:pointer;font-family:var(--mono);font-size:9px;letter-spacing:1.5px;text-transform:uppercase;flex-shrink:0;transition:all .15s}
.gesture-row .test:hover{background:var(--accent);color:#fff;border-color:var(--accent)}
.gesture-row .test:active{transform:scale(.95)}
.gesture-slot{min-height:38px;background:#06060a;border:1px dashed var(--ink-4);border-radius:4px;padding:5px 8px;display:flex;align-items:center;gap:6px;transition:all .15s}
.gesture-slot.drop-over{border-color:var(--accent);background:var(--accent-soft);border-style:solid}
.gesture-slot .assigned{background:var(--accent-soft);border:1px solid var(--accent);color:var(--ink);padding:4px 10px;border-radius:2px;font-size:11px;display:inline-flex;align-items:center;gap:8px;font-weight:400}
.gesture-slot .assigned.def{background:transparent;border-color:var(--ink-4);color:var(--ink-3);border-style:dashed}
.gesture-slot .assigned .x{cursor:pointer;color:var(--ink-3);font-size:14px;line-height:1;padding-left:4px;border-left:1px solid var(--ink-4)}
.gesture-slot .assigned .x:hover{color:var(--accent)}
.gesture-slot .assigned input{width:48px;background:#06060a;border:1px solid var(--ink-4);color:var(--ink);border-radius:2px;font-size:11px;padding:2px 4px;text-align:center;font-family:var(--mono)}
.gesture-slot .empty-hint{font-family:var(--mono);font-size:10px;color:var(--ink-4);letter-spacing:1px;text-transform:uppercase;margin:0 auto}
.sound-row{display:flex;align-items:center;gap:14px;padding:14px 0}
.sound-row:not(:last-child){border-bottom:1px solid var(--ink-4)}
.sound-row .slabel{font-family:var(--serif);font-size:14px;font-weight:500;color:var(--ink);width:90px;flex-shrink:0;letter-spacing:-.2px}
.sound-row .sval{font-family:var(--mono);font-size:14px;color:var(--accent);font-weight:600;width:42px;text-align:right;flex-shrink:0;font-variant-numeric:tabular-nums;letter-spacing:.5px}
.sound-row input[type=range]{flex:1;-webkit-appearance:none;background:transparent;height:24px;cursor:pointer}
.sound-row input[type=range]::-webkit-slider-runnable-track{height:2px;background:var(--ink-4);border-radius:1px}
.sound-row input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:14px;height:14px;background:var(--accent);border-radius:50%;margin-top:-6px;cursor:pointer;border:none;box-shadow:0 0 0 4px var(--surface), 0 2px 6px rgba(123,45,45,.4)}
.sound-row input[type=range]::-moz-range-track{height:2px;background:var(--ink-4);border-radius:1px}
.sound-row input[type=range]::-moz-range-thumb{width:14px;height:14px;background:var(--accent);border-radius:50%;cursor:pointer;border:none;box-shadow:0 0 0 4px var(--surface), 0 2px 6px rgba(123,45,45,.4)}
.sound-row .sw{position:relative;width:38px;height:22px;background:var(--rail);border-radius:11px;cursor:pointer;flex-shrink:0;transition:background .2s;border:1px solid var(--ink-4)}
.sound-row .sw.on{background:var(--accent);border-color:var(--accent)}
.sound-row .sw::after{content:'';position:absolute;top:2px;left:2px;width:16px;height:16px;background:var(--ink-3);border-radius:50%;transition:all .2s}
.sound-row .sw.on::after{left:18px;background:#fff}
</style></head><body>
<header class='masthead'>
<div class='logo'><img src='/logo.png' alt='TPS Audio'></div>
<div class='eyebrow'>Sonos · Ethernet · Edition</div>
<div class='room' id='room'><span class='actdot' id='actdot' title='Lights up when this board sees a knob turn or click'></span><span class='roomtxt' id='roomtxt'>···</span><span class='roomhost' id='roomhost'></span></div>
<div class='dev' id='dev' onclick='rename()' title='Click to rename'></div>
<div id='fwline' style="font-family:var(--mono);font-size:9px;color:var(--ink-3);text-align:center;letter-spacing:1px;margin-top:4px"></div>
<div class='steprow' title='How much the volume changes per encoder detent'>
  <span>Vol/detent</span>
  <input type='range' min='1' max='10' step='1' id='stepsld' oninput='onStepInput(this.value)' onchange='onStepCommit(this.value)'>
  <span class='stepval' id='stepval'>—</span>
</div>
<div class='invrow' title='Flip knob direction if rotation feels backwards on this board'>
  <span>Invert rotation</span>
  <div class='toggle' id='invtgl' onclick='toggleInv()'></div>
</div>
</header>
<div class='col-narrow'>
<div class='card'>
<div class='gauge-wrap'>
<div class='gauge' id='gauge' title='Click and drag up/down to change volume — also scrolls'>
<svg viewBox='0 0 120 120'>
<path class='track' d='M42.9 107A50 50 0 1 1 77.1 107'/>
<path class='fill' id='arc' d='M42.9 107A50 50 0 1 1 77.1 107' stroke-dasharray='279.3' stroke-dashoffset='279.3'/>
</svg>
<div class='center'>
<div class='num' id='vol'>--</div>
<div class='label' id='vlbl'>volume</div>
</div>
</div>
</div>
<div class='transport'>
<div class='tbtn' onclick='cmd("prev")' title='Previous'><svg viewBox='0 0 24 24'><path d='M6 6h2v12H6zm3.5 6l8.5 6V6z'/></svg></div>
<div class='tbtn play' onclick='cmd("play")' title='Play/Pause' id='btnplay'><svg viewBox='0 0 24 24'><path d='M8 5v14l11-7z'/></svg></div>
<div class='tbtn' onclick='cmd("next")' title='Next'><svg viewBox='0 0 24 24'><path d='M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z'/></svg></div>
<div class='tbtn' onclick='cmd("mute")' title='Mute' id='btnmute'><svg viewBox='0 0 24 24'><path d='M3 9v6h4l5 5V4L7 9H3zm13.5 3c0-1.77-1.02-3.29-2.5-4.03v8.05c1.48-.73 2.5-2.25 2.5-4.02z'/></svg></div>
</div>
<div class='meta'>
<div class='spkname' id='spkn'>—</div>
<div class='badges' id='badges'></div>
<div id='modepill' class='modepill' style='display:none'></div>
</div>
</div>
<div class='card' id='npcard' style='display:none'><div class='np'>
<div id='npart'></div>
<div class='info'>
<div class='t' id='npt'></div>
<div class='a' id='npa'></div>
<div class='al' id='npal'></div>
<div class='time' id='nptime'></div>
</div>
</div></div>
<div class='scan' id='scan'></div>
<div id='list'></div>
<div class='card btns'>
<div class='btn' onclick='disc()'>Discover</div>
<div class='btn' onclick='location.reload()'>Refresh</div>
</div>
<details class='section' open>
<summary>Sound Shape <span>bass · treble · loudness</span></summary>
<div class='section-body'>
<div class='sound-row'>
<div class='slabel'>Bass</div>
<input type='range' id='slBass' min='-10' max='10' step='1' value='0' oninput='setBass(this.value)'>
<div class='sval' id='vBass'>0</div>
</div>
<div class='sound-row'>
<div class='slabel'>Treble</div>
<input type='range' id='slTreble' min='-10' max='10' step='1' value='0' oninput='setTreble(this.value)'>
<div class='sval' id='vTreble'>0</div>
</div>
<div class='sound-row'>
<div class='slabel'>Loudness</div>
<div style='flex:1'></div>
<div class='sw' id='swLoud' onclick='toggleLoud()'></div>
</div>
</div>
</details>
</div><!-- /col-narrow -->
<div class='col-wide'>
<div class='workspace'>
<section class='ws-col'>
<div class='ws-head'><h3>Gestures</h3><span class='sub'>Input · Bindings</span></div>
<div class='gesture-list' id='gestureBody'></div>
</section>
<div class='rail'></div>
<section class='ws-col'>
<div class='ws-head'><h3>Actions</h3><span class='sub'>Library · Drag or Try</span></div>
<div class='filter-chips' id='filterChips'></div>
<div class='actions-list' id='actionsBody'></div>
</section>
</div>
</div>
<div class='col-narrow'>
<div class='log' id='log'></div>
</div>
<script>
const ARC=279.3;
let lastVol=-1,lastLog='';

function setGauge(vol,muted){
  let arc=document.getElementById('arc');
  arc.style.strokeDashoffset=ARC-(vol/100*ARC);
  arc.style.stroke=muted?'#a55':vol>80?'#c44':vol>50?'#7B2D2D':'#4a3535';
  let num=document.getElementById('vol');
  num.textContent=vol;
  num.className='num'+(muted?' mut':'');
  document.getElementById('vlbl').textContent=muted?'muted':'volume';
}

function poll(){
  fetch('/api/status').then(r=>r.json()).then(d=>{
    if(dragY===null){setGauge(d.vol,d.mut);lastVol=d.vol;}
    document.getElementById('btnplay').innerHTML=d.play?
      "<svg viewBox='0 0 24 24'><path d='M6 4h4v16H6zm8 0h4v16h-4z'/></svg>":
      "<svg viewBox='0 0 24 24'><path d='M8 5v14l11-7z'/></svg>";
    let mb=document.getElementById('btnmute');
    mb.classList.toggle('muted',d.mut);
    // Mode pill
    let mp=document.getElementById('modepill');
    if(d.mode && d.mode!=='volume'){
      mp.style.display='inline-flex';
      mp.className='modepill '+d.mode;
      mp.innerHTML="<span class='mp-dot'></span>"+d.mode+" · "+(d.modeVal>=0?'+'+d.modeVal:d.modeVal);
    }else{
      mp.style.display='none';
    }
    // Sound sliders (don't fight the user while they drag)
    if(!soundDragging){
      let sb=document.getElementById('slBass'),vb=document.getElementById('vBass');
      if(sb && document.activeElement!==sb){sb.value=d.bass;vb.textContent=d.bass>0?'+'+d.bass:d.bass;}
      let st=document.getElementById('slTreble'),vt=document.getElementById('vTreble');
      if(st && document.activeElement!==st){st.value=d.treble;vt.textContent=d.treble>0?'+'+d.treble:d.treble;}
    }
    document.getElementById('spkn').textContent=d.spk||'No speaker';
    let rm=document.getElementById('room'),rt=document.getElementById('roomtxt'),rh=document.getElementById('roomhost'),ad=document.getElementById('actdot');
    if(d.roomSlug && d.roomSlug.length>0){
      rm.className='room';
      rt.textContent=(d.roomLabel && d.roomLabel.length>0)?d.roomLabel:d.roomSlug;
    } else {
      rm.className='room unassigned';
      rt.textContent='Unassigned';
    }
    rh.textContent=d.host+'.local';
    // Activity dot — green pulse when the user just touched this knob.
    // sinceAct=-1 means no activity ever since boot. We keep the dot lit for
    // a full 1200ms from the event itself so it survives between 1Hz polls.
    if(typeof d.sinceAct==='number' && d.sinceAct>=0 && d.sinceAct<1200){
      ad.classList.add('live');
      if(window._actTimer)clearTimeout(window._actTimer);
      window._actTimer=setTimeout(()=>ad.classList.remove('live'),1200-d.sinceAct);
    }
    document.getElementById('dev').innerHTML="<span class='dot'></span>"+(d.dev||d.host);
    let it=document.getElementById('invtgl');
    if(it){ if(d.inv) it.classList.add('on'); else it.classList.remove('on'); }
    let sld=document.getElementById('stepsld'),sv=document.getElementById('stepval');
    if(sld && typeof d.step==='number' && !stepDragging){
      sld.value=d.step;
      sv.textContent=d.step;
    }
    // Gesture-fired flash. Dedup by (gid + event-timestamp) so we only fire
    // the animation once per actual gesture, not on every status poll.
    if(d.firedGid && d.firedSince>=0){
      let eventStamp=Math.round((d.up*1000) - d.firedSince);
      let key=d.firedGid+':'+eventStamp;
      if(key!==window._lastFlashKey){
        window._lastFlashKey=key;
        let slot=document.querySelector('.gesture-slot[data-gid="'+d.firedGid+'"]');
        let row=slot ? slot.closest('.gesture-row') : null;
        if(row){
          row.classList.remove('flash-ok','flash-fail');
          // Force a reflow so the animation re-runs even when same class re-applied.
          void row.offsetWidth;
          row.classList.add(d.firedOk?'flash-ok':'flash-fail');
        }
      }
    }
    let fw=document.getElementById('fwline');
    if(fw){
      let extra='';
      if(d.updStatus && d.updStatus!=='idle' && d.updStatus!=='up_to_date'){
        extra=' · '+d.updStatus;
      } else if(d.updLatest && d.updLatest!==d.fwver){
        extra=' · update available: '+d.updLatest;
      }
      fw.innerHTML='FW '+d.fwver+extra+' · <a href="#" onclick="checkUpdate();return false" style="color:inherit;text-decoration:underline">check now</a>';
    }
    let b='';
    b+=d.play?"<span class='on'>Playing</span>":"<span class='off'>Paused</span>";
    if(d.mut)b+="<span class='mut'>Muted</span>";
    document.getElementById('badges').innerHTML=b;
    let nc=document.getElementById('npcard');
    if(d.title){
      nc.style.display='block';
      document.getElementById('npt').textContent=d.title;
      document.getElementById('npa').textContent=d.artist||'';
      document.getElementById('npal').textContent=d.album||'';
      document.getElementById('nptime').textContent=(d.elapsed||'')+' / '+(d.dur||'');
      let artEl=document.getElementById('npart');
      if(d.art){
        if(!artEl.querySelector('img')||artEl.querySelector('img').src!==d.art)
          artEl.innerHTML="<img src='"+d.art+"' alt=''>";
      }else{
        artEl.innerHTML="<div class='noart'>&#9835;</div>";
      }
    }else{nc.style.display='none';}
  }).catch(()=>{});
}

function pollLog(){
  fetch('/api/log').then(r=>r.text()).then(t=>{
    if(t!==lastLog){
      lastLog=t;
      let el=document.getElementById('log');
      el.textContent=t;
      el.scrollTop=el.scrollHeight;
    }
  }).catch(()=>{});
}

function pollSlow(){
  fetch('/api/speakers').then(r=>r.json()).then(d=>{
    let h='';
    d.list.forEach(s=>{
      let a=s.name===d.cur?' act':'';
      h+="<div class='card spk"+a+"' onclick='sel(\""+s.ip+"\",\""+s.name+"\")'><div class='sn'>"+s.name+"</div><div class='si'>"+s.ip+"</div></div>";
    });
    document.getElementById('list').innerHTML=h;
  }).catch(()=>{});
  fetch('/api/scan').then(r=>r.json()).then(d=>{
    document.getElementById('scan').textContent=d.active?d.msg:'';
  }).catch(()=>{});
}

function sel(ip,nm){fetch('/api/select?ip='+ip+'&name='+encodeURIComponent(nm)).then(()=>{poll();pollSlow();});}
function disc(){fetch('/api/discover');setTimeout(()=>{pollSlow();},4000);}
function rename(){let n=prompt('Name this controller:');if(n)fetch('/api/name?name='+encodeURIComponent(n)).then(poll);}
function toggleInv(){
  let cur=document.getElementById('invtgl').classList.contains('on');
  fetch('/api/setinvert?v='+(cur?'0':'1')).then(poll);
}
function checkUpdate(){fetch('/api/checkupdate').then(()=>setTimeout(poll,800));}
let stepDragging=false;
function onStepInput(v){
  document.getElementById('stepval').textContent=v;
  stepDragging=true;
}
function onStepCommit(v){
  fetch('/api/setstep?v='+v).then(()=>{stepDragging=false;poll();});
}

// --- Volume + transport controls ---
function cmd(action){fetch('/api/'+action).then(()=>setTimeout(poll,200));}

// Throttled absolute volume send (used by drag + wheel)
let volTimer=null;
function sendVol(v){
  if(volTimer)clearTimeout(volTimer);
  volTimer=setTimeout(()=>{fetch('/api/vol?v='+v).then(()=>poll());},80);
}

// Scroll wheel on gauge — instant feedback, throttled send
let g=document.getElementById('gauge');
g.addEventListener('wheel',e=>{
  e.preventDefault();
  let step=e.deltaY<0?2:-2;
  let v=Math.max(0,Math.min(100,(lastVol||0)+step));
  setGauge(v,false);lastVol=v;
  sendVol(v);
},{passive:false});

// Click-and-drag on gauge: vertical drag = volume (Logic-style)
let dragY=null,dragStartVol=0;
function gStart(y){dragY=y;dragStartVol=lastVol||0;}
function gMove(y){
  if(dragY===null)return;
  let dy=dragY-y;
  // 2px per volume unit — comfortable, predictable
  let target=Math.max(0,Math.min(100,dragStartVol+Math.round(dy/2)));
  if(target!==lastVol){
    setGauge(target,false);
    lastVol=target;
    sendVol(target);  // continuous throttled send while dragging
  }
}
function gEnd(){
  if(dragY===null)return;
  dragY=null;
  // Final commit to be sure
  fetch('/api/vol?v='+lastVol).then(()=>poll());
}
g.addEventListener('mousedown',e=>{e.preventDefault();gStart(e.clientY);});
window.addEventListener('mousemove',e=>gMove(e.clientY));
window.addEventListener('mouseup',gEnd);
g.addEventListener('touchstart',e=>{e.preventDefault();gStart(e.touches[0].clientY);},{passive:false});
g.addEventListener('touchmove',e=>{e.preventDefault();gMove(e.touches[0].clientY);},{passive:false});
g.addEventListener('touchend',gEnd);

// --- Gesture mappings + action library ---
let allActions=[];
let gestureMappings={};
const GESTURES=[
  {id:'1c',   label:'1 click'},
  {id:'2c',   label:'2 clicks'},
  {id:'3c',   label:'3 clicks'},
  {id:'4c',   label:'4 clicks'},
  {id:'5c',   label:'5 clicks'},
  {id:'1c+h', label:'click + hold'},
  {id:'2c+h', label:'2c + hold'},
  {id:'3c+h', label:'3c + hold'},
  {id:'4c+h', label:'4c + hold'},
  {id:'hold', label:'hold (700ms)'},
  {id:'lh',   label:'long hold (2s)'},
];

function loadActions(){
  return fetch('/api/actions').then(r=>r.json()).then(a=>{allActions=a;renderFilters();renderActions();});
}
function loadMappings(){
  return fetch('/api/mappings').then(r=>r.json()).then(m=>{gestureMappings=m;renderGestures();});
}

function actionLabel(id){let a=allActions.find(x=>x.id===id);return a?a.label:id;}
function actionHint(id){let a=allActions.find(x=>x.id===id);return a?a.hint:'';}

const CAT_LABELS={playback:'Playback',volume:'Volume',knob:'Knob',eq:'EQ',speaker:'Speaker',playmode:'Mode',sleep:'Sleep'};
const CAT_ORDER=['playback','volume','knob','eq','speaker','playmode','sleep'];
let activeFilter='all';

function renderFilters(){
  let cats=new Set(allActions.map(a=>a.cat));
  let h="<div class='chip"+(activeFilter==='all'?' active':'')+"' onclick='setFilter(\"all\")'>All</div>";
  CAT_ORDER.forEach(c=>{
    if(!cats.has(c))return;
    h+="<div class='chip"+(activeFilter===c?' active':'')+"' onclick='setFilter(\""+c+"\")'>"+CAT_LABELS[c]+"</div>";
  });
  document.getElementById('filterChips').innerHTML=h;
}
function setFilter(c){activeFilter=c;renderFilters();renderActions();}

function renderActions(){
  let h='';
  let list=activeFilter==='all'?allActions:allActions.filter(a=>a.cat===activeFilter);
  list.forEach(a=>{
    let p=a.hint?"<input type='text' placeholder='"+a.hint+"' onclick='event.stopPropagation()' onmousedown='event.stopPropagation()'>":"";
    h+="<div class='action-pill' draggable='true' data-id='"+a.id+"' ondragstart='dragStart(event,\""+a.id+"\")' ondragend='dragEnd(event)' onclick='tryAction(event,this,\""+a.id+"\")'>"
      +"<span class='alabel'>"+a.label+"</span>"+p
      +"<span class='acat'>"+(CAT_LABELS[a.cat]||a.cat)+"</span>"
      +"<span class='try' title='Try'>▶</span>"
      +"</div>";
  });
  document.getElementById('actionsBody').innerHTML=h;
}

function tryAction(ev,el,id){
  if(ev.target.tagName==='INPUT')return;
  ev.stopPropagation();
  let inp=el.querySelector('input');
  let p=inp?inp.value:'';
  fetch('/api/action?id='+id+'&p='+encodeURIComponent(p)).then(()=>setTimeout(poll,200));
}

function renderGestures(){
  let h='';
  GESTURES.forEach(g=>{
    let m=gestureMappings[g.id]||{};
    // Split label into ID + descriptor for two-line gname
    let parts=g.label.split(' (');
    let mainLabel=parts[0];
    let subLabel=parts[1]?parts[1].replace(')',''):'';
    h+="<div class='gesture-row'>";
    h+="<div class='gname'>"+mainLabel+(subLabel?"<small>"+subLabel+"</small>":'')+"</div>";
    h+="<div class='gesture-slot' data-gid='"+g.id+"' ondragover='dragOver(event)' ondragleave='dragLeave(event)' ondrop='drop(event,\""+g.id+"\")'>";
    if(m.action){
      let cls='assigned'+(m.default?' def':'');
      let pi=actionHint(m.action)?
        "<input type='text' value='"+(m.param||'')+"' placeholder='"+actionHint(m.action)+"' onchange='setMap(\""+g.id+"\",\""+m.action+"\",this.value)'>":'';
      h+="<div class='"+cls+"'>"+actionLabel(m.action)+pi
        +"<span class='x' onclick='clearMap(\""+g.id+"\")' title='Reset to default'>×</span></div>";
    }else{
      h+="<span class='empty-hint'>drop action here</span>";
    }
    h+="</div>";
    h+="<button class='test' onclick='testGesture(\""+g.id+"\")'>Test</button>";
    h+="</div>";
  });
  document.getElementById('gestureBody').innerHTML=h;
}

function testGesture(gid){
  fetch('/api/gesture?g='+encodeURIComponent(gid)).then(()=>setTimeout(poll,200));
}

// --- Sound shape sliders ---
let soundDragging=false,soundTimer=null;
function setBass(v){
  document.getElementById('vBass').textContent=v>0?'+'+v:v;
  soundDragging=true;
  if(soundTimer)clearTimeout(soundTimer);
  soundTimer=setTimeout(()=>{
    fetch('/api/bass?v='+v).then(()=>{soundDragging=false;poll();});
  },120);
}
function setTreble(v){
  document.getElementById('vTreble').textContent=v>0?'+'+v:v;
  soundDragging=true;
  if(soundTimer)clearTimeout(soundTimer);
  soundTimer=setTimeout(()=>{
    fetch('/api/treble?v='+v).then(()=>{soundDragging=false;poll();});
  },120);
}
let loudState=false;
function toggleLoud(){
  loudState=!loudState;
  document.getElementById('swLoud').classList.toggle('on',loudState);
  fetch('/api/loudness?v='+(loudState?1:0));
}
function refreshSound(){
  fetch('/api/sound').then(r=>r.json()).then(d=>{
    let sb=document.getElementById('slBass');if(sb){sb.value=d.bass;document.getElementById('vBass').textContent=d.bass>0?'+'+d.bass:d.bass;}
    let st=document.getElementById('slTreble');if(st){st.value=d.treble;document.getElementById('vTreble').textContent=d.treble>0?'+'+d.treble:d.treble;}
    loudState=!!d.loudness;
    document.getElementById('swLoud').classList.toggle('on',loudState);
  }).catch(()=>{});
}

let draggingId=null;
function dragStart(e,id){draggingId=id;e.dataTransfer.effectAllowed='copy';e.target.classList.add('dragging');}
function dragEnd(e){e.target.classList.remove('dragging');document.querySelectorAll('.gesture-slot').forEach(s=>s.classList.remove('drop-over'));}
function dragOver(e){e.preventDefault();e.currentTarget.classList.add('drop-over');}
function dragLeave(e){e.currentTarget.classList.remove('drop-over');}
function drop(e,gid){
  e.preventDefault();
  e.currentTarget.classList.remove('drop-over');
  if(!draggingId)return;
  setMap(gid,draggingId,'');
}
function setMap(gid,aid,p){
  fetch('/api/setmap?g='+gid+'&a='+aid+'&p='+encodeURIComponent(p||'')).then(()=>loadMappings());
}
function clearMap(gid){
  fetch('/api/setmap?g='+gid+'&a=default').then(()=>loadMappings());
}

loadActions().then(loadMappings);
refreshSound();

poll();pollLog();pollSlow();
setInterval(poll,1000);
setInterval(pollLog,2000);
setInterval(pollSlow,10000);
</script></body></html>)rawliteral";
  web.send_P(200, "text/html", HTML);
}

static void serveLogo() {
  // Decode base64 to binary PNG and serve with cache headers
  // For simplicity, serve as base64 data URI redirect — browser caches the PNG
  web.sendHeader("Cache-Control", "public, max-age=86400");
  web.sendHeader("Content-Type", "image/png");

  // Decode base64 in chunks
  static const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  auto b64val = [](char c) -> uint8_t {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return 0;
  };

  // Calculate decoded size
  size_t b64len = LOGO_B64_LEN;
  size_t pad = 0;
  if (b64len > 0 && pgm_read_byte(&LOGO_B64[b64len-1]) == '=') pad++;
  if (b64len > 1 && pgm_read_byte(&LOGO_B64[b64len-2]) == '=') pad++;
  size_t decoded_len = (b64len / 4) * 3 - pad;

  web.setContentLength(decoded_len);
  web.send(200, "image/png", "");

  // Stream decode
  uint8_t out[3];
  for (size_t i = 0; i < b64len; i += 4) {
    uint8_t a = b64val(pgm_read_byte(&LOGO_B64[i]));
    uint8_t b = b64val(pgm_read_byte(&LOGO_B64[i+1]));
    uint8_t c = b64val(pgm_read_byte(&LOGO_B64[i+2]));
    uint8_t d = b64val(pgm_read_byte(&LOGO_B64[i+3]));
    out[0] = (a << 2) | (b >> 4);
    out[1] = (b << 4) | (c >> 2);
    out[2] = (c << 6) | d;
    size_t remaining = decoded_len - ((i/4)*3);
    size_t n = (remaining >= 3) ? 3 : remaining;
    web.client().write(out, n);
  }
}

static String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else out += c;
  }
  return out;
}

static void serveApiStatus() {
  String json;
  json.reserve(640);
  json = "{\"vol\":"; json += spk.volume;
  json += ",\"mut\":"; json += spk.muted ? "true" : "false";
  json += ",\"play\":"; json += spk.playing ? "true" : "false";
  json += ",\"spk\":\""; json += jsonEscape(spk.name);
  json += "\",\"dev\":\""; json += jsonEscape(deviceName);
  json += "\",\"host\":\""; json += deviceHostname;
  // Room assignment — slug + human label so the UI can render the right banner
  // and tell at a glance whether this board has been assigned yet.
  {
    String slug = loadRoomSlug();
    json += "\",\"roomSlug\":\""; json += jsonEscape(slug);
    json += "\",\"roomLabel\":\""; json += jsonEscape(slug.length() ? String(labelForSlug(slug.c_str())) : String(""));
  }
  json += "\",\"ip\":\""; json += ETH.localIP().toString();
  json += "\",\"title\":\""; json += jsonEscape(spk.title);
  json += "\",\"artist\":\""; json += jsonEscape(spk.artist);
  json += "\",\"album\":\""; json += jsonEscape(spk.album);
  json += "\",\"art\":\""; json += jsonEscape(spk.artURL);
  json += "\",\"dur\":\""; json += spk.duration;
  json += "\",\"elapsed\":\""; json += spk.elapsed;
  json += "\",\"mode\":\""; json += modeName(currentMode);
  json += "\",\"modeVal\":"; json += currentModeValue();
  json += ",\"bass\":"; json += modeBassCache;
  json += ",\"treble\":"; json += modeTrebleCache;
  json += ",\"up\":"; json += millis() / 1000;
  // ms since last physical knob interaction — UI uses this to light an
  // "I am the one you're touching" indicator. -1 means no activity ever yet.
  {
    unsigned long actMs = lastActivityMs;
    long since = actMs == 0 ? -1 : (long)(millis() - actMs);
    json += ",\"sinceAct\":"; json += since;
  }
  json += ",\"inv\":"; json += encoderInvert ? "true" : "false";
  json += ",\"step\":"; json += volumeStep;
  // Firmware version + OTA updater state.
  json += ",\"fwver\":\""; json += FW_VERSION; json += "\"";
  json += ",\"updStatus\":\""; json += jsonEscape(updaterState.status); json += "\"";
  json += ",\"updLatest\":\""; json += jsonEscape(updaterState.latestVer); json += "\"";
  json += ",\"updError\":\""; json += jsonEscape(updaterState.lastError); json += "\"";
  // Last-fired gesture for UI flash — only report if it happened in the last
  // 2s, otherwise null so the UI doesn't re-flash stale events on every poll.
  {
    unsigned long since = millis() - lastFiredMs;
    if (lastFiredMs > 0 && since < 2000) {
      json += ",\"firedGid\":\""; json += jsonEscape(lastFiredGid); json += "\"";
      json += ",\"firedOk\":"; json += lastFiredOk ? "true" : "false";
      json += ",\"firedSince\":"; json += since;
    } else {
      json += ",\"firedGid\":\"\",\"firedOk\":false,\"firedSince\":-1";
    }
  }
  json += "}";
  web.send(200, "application/json", json);
}

// Set bass/treble/loudness directly from the web UI (sliders)
static void serveApiBass() {
  if (!web.hasArg("v")) { web.send(400, "text/plain", "missing v"); return; }
  if (!spk.connected()) { web.send(503, "text/plain", "no speaker"); return; }
  int v = constrain(web.arg("v").toInt(), -10, 10);
  ctrl.ip = spk.ip;
  ctrl.setBass(v);
  modeBassCache = v;
  logEvent("bass -> %d (web)", v);
  web.send(200, "application/json", "{\"ok\":true}");
}

static void serveApiTreble() {
  if (!web.hasArg("v")) { web.send(400, "text/plain", "missing v"); return; }
  if (!spk.connected()) { web.send(503, "text/plain", "no speaker"); return; }
  int v = constrain(web.arg("v").toInt(), -10, 10);
  ctrl.ip = spk.ip;
  ctrl.setTreble(v);
  modeTrebleCache = v;
  logEvent("treble -> %d (web)", v);
  web.send(200, "application/json", "{\"ok\":true}");
}

static void serveApiLoudness() {
  if (!web.hasArg("v")) { web.send(400, "text/plain", "missing v"); return; }
  if (!spk.connected()) { web.send(503, "text/plain", "no speaker"); return; }
  bool on = web.arg("v") == "1" || web.arg("v") == "true";
  ctrl.ip = spk.ip;
  ctrl.setLoudness(on);
  logEvent("loudness -> %s (web)", on ? "on" : "off");
  web.send(200, "application/json", "{\"ok\":true}");
}

// Refresh bass/treble cache from speaker (called when sliders card is opened)
static void serveApiSoundRefresh() {
  if (!spk.connected()) { web.send(503, "text/plain", "no speaker"); return; }
  ctrl.ip = spk.ip;
  modeBassCache = ctrl.getBass();
  modeTrebleCache = ctrl.getTreble();
  bool loud = ctrl.getLoudness();
  String r = "{\"bass\":"; r += modeBassCache;
  r += ",\"treble\":"; r += modeTrebleCache;
  r += ",\"loudness\":"; r += loud ? "true" : "false";
  r += "}";
  web.send(200, "application/json", r);
}

// Fire a gesture from the web UI (Test button)
static void serveApiGesture() {
  if (!web.hasArg("g")) { web.send(400, "text/plain", "missing g"); return; }
  String g = web.arg("g");
  bool ok = runGesture(g.c_str());
  String r = "{\"ok\":";
  r += ok ? "true" : "false";
  r += "}";
  web.send(200, "application/json", r);
}

static void serveApiSpeakers() {
  String json;
  json.reserve(128 + speakers.size() * 64);
  json = "{\"cur\":\"";
  json += spk.name;
  json += "\",\"list\":[";
  for (size_t i = 0; i < speakers.size(); i++) {
    if (i) json += ',';
    json += "{\"name\":\"";
    json += speakers[i].name;
    json += "\",\"ip\":\"";
    json += speakers[i].ip;
    json += "\"}";
  }
  json += "]}";
  web.send(200, "application/json", json);
}

static void serveApiSelect() {
  if (!web.hasArg("ip")) { web.send(400, "text/plain", "missing ip"); return; }
  String ip = web.arg("ip");
  for (size_t i = 0; i < speakers.size(); i++) {
    if (speakers[i].ip == ip) {
      selectSpeaker(i);
      logEvent("selected: %s", speakers[i].name.c_str());
      web.send(200, "application/json", "{\"ok\":true}");
      return;
    }
  }
  web.send(404, "text/plain", "not found");
}

static void serveApiDiscover() {
  scanRequested = true;
  web.send(200, "application/json", "{\"ok\":true}");
}

static void serveApiReset() {
  Preferences p;
  p.begin("sonos", false);
  p.remove("ip");
  p.remove("name");
  p.end();
  spk = SpeakerState();
  speakers.clear();
  logEvent("speaker assignment cleared");
  web.send(200, "application/json", "{\"ok\":true}");
}

static void serveApiScan() {
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"active\":%s,\"msg\":\"%s\",\"found\":%d}",
    scanActive ? "true" : "false", scanMsg.c_str(), (int)speakers.size());
  web.send(200, "application/json", buf);
}

static void serveApiName() {
  if (!web.hasArg("name")) { web.send(400, "text/plain", "missing name"); return; }
  deviceName = web.arg("name");
  Preferences p;
  p.begin("sonos", false);
  p.putString("devname", deviceName);
  p.end();
  logEvent("renamed: %s", deviceName.c_str());
  web.send(200, "application/json", "{\"ok\":true}");
}

// --- Room assignment (drives mDNS hostname) ---
static void serveApiRooms() {
  String slug = loadRoomSlug();
  String json;
  json.reserve(64 + ROOMS_COUNT * 64);
  json = "{\"current\":\"";
  json += slug;
  json += "\",\"hostname\":\"";
  json += deviceHostname;
  json += "\",\"rooms\":[";
  for (size_t i = 0; i < ROOMS_COUNT; i++) {
    if (i) json += ',';
    json += "{\"slug\":\"";
    json += ROOMS[i].slug;
    json += "\",\"label\":\"";
    json += ROOMS[i].label;
    json += "\"}";
  }
  json += "]}";
  web.send(200, "application/json", json);
}

// Atomically set prefix + slug in one shot. Replaces the older two-call dance
// of /api/setprefix → reboot → /api/setroom → reboot. Single restart at the end.
static void serveApiSetup() {
  String prefix = web.hasArg("prefix") ? web.arg("prefix") : String("");
  String slug   = web.hasArg("slug")   ? web.arg("slug")   : String("");
  if (prefix.length() > 0 && !isValidRoomSlug(prefix)) {
    web.send(400, "text/plain", "invalid prefix");
    return;
  }
  if (slug.length() > 0 && !isValidRoomSlug(slug)) {
    web.send(400, "text/plain", "invalid slug");
    return;
  }
  if (!saveHostPrefix(prefix) || !saveRoomSlug(slug)) {
    web.send(500, "text/plain", "NVS write failed");
    return;
  }
  logEvent("setup prefix='%s' slug='%s' — restarting",
    prefix.length() ? prefix.c_str() : "(default)",
    slug.length() ? slug.c_str() : "(cleared)");
  web.send(200, "application/json", "{\"ok\":true,\"restarting\":true}");
  delay(250);
  ESP.restart();
}

static void serveApiSetPrefix() {
  String prefix = web.hasArg("p") ? web.arg("p") : (web.hasArg("prefix") ? web.arg("prefix") : String(""));
  if (prefix.length() > 0 && !isValidRoomSlug(prefix)) {
    web.send(400, "text/plain", "invalid prefix");
    return;
  }
  if (!saveHostPrefix(prefix)) {
    web.send(500, "text/plain", "NVS write failed");
    return;
  }
  logEvent("prefix set to '%s' — restarting", prefix.length() ? prefix.c_str() : "(default)");
  web.send(200, "application/json", "{\"ok\":true,\"restarting\":true}");
  delay(250);
  ESP.restart();
}

static void serveApiSetRoom() {
  String slug = web.hasArg("slug") ? web.arg("slug") : String("");
  // Empty slug = clear assignment (board returns to sonos-p4-XXXX fallback).
  if (slug.length() > 0 && !isValidRoomSlug(slug)) {
    web.send(400, "text/plain", "invalid slug — lowercase letters/digits/hyphens, ≤16 chars");
    return;
  }
  if (!saveRoomSlug(slug)) {
    web.send(500, "text/plain", "NVS write failed");
    return;
  }
  logEvent("room set to '%s' — restarting", slug.length() ? slug.c_str() : "(cleared)");
  web.send(200, "application/json", "{\"ok\":true,\"restarting\":true}");
  delay(250);  // give the response time to flush
  ESP.restart();
}

static void serveApiRestart() {
  logEvent("restart requested via API");
  web.send(200, "application/json", "{\"ok\":true}");
  delay(250);
  ESP.restart();
}

// Manual update trigger. ?force=1 will apply even if manifest version is the
// same as ours (useful for re-flashing or rolling back via a hand-edited manifest).
static void serveApiCheckUpdate() {
  bool force = web.hasArg("force") && web.arg("force") == "1";
  web.send(200, "application/json", "{\"ok\":true,\"checking\":true}");
  // checkForUpdate may reboot on success — flush before calling.
  delay(100);
  checkForUpdate(force);
}

// Set volume step (1..10). Persisted to NVS, takes effect immediately.
static void serveApiSetStep() {
  if (!web.hasArg("v")) { web.send(400, "text/plain", "missing v"); return; }
  int v = web.arg("v").toInt();
  if (v < 1) v = 1;
  if (v > 10) v = 10;
  volumeStep = v;
  saveVolumeStep(v);
  logEvent("volume step = %d", v);
  web.send(200, "application/json", String("{\"ok\":true,\"step\":") + v + "}");
}

// Toggle/set rotation inversion. Persisted to NVS, takes effect immediately
// — no reboot needed since processEncoder() reads `encoderInvert` every tick.
static void serveApiSetInvert() {
  if (!web.hasArg("v")) { web.send(400, "text/plain", "missing v=0|1"); return; }
  String v = web.arg("v");
  bool inv = (v == "1" || v == "true" || v == "on");
  encoderInvert = inv;
  saveEncoderInvert(inv);
  logEvent("encoder invert = %d", inv ? 1 : 0);
  web.send(200, "application/json", String("{\"ok\":true,\"inv\":") + (inv ? "true" : "false") + "}");
}

// --- Action registry + gesture mappings ---
static void serveApiActions() {
  String json;
  json.reserve(64 + ACTIONS_COUNT * 96);
  json = "[";
  for (size_t i = 0; i < ACTIONS_COUNT; i++) {
    if (i) json += ',';
    json += "{\"id\":\"";  json += ACTIONS[i].id;
    json += "\",\"label\":\""; json += ACTIONS[i].label;
    json += "\",\"cat\":\"";   json += ACTIONS[i].category;
    json += "\",\"hint\":\"";  json += ACTIONS[i].paramHint;
    json += "\"}";
  }
  json += "]";
  web.send(200, "application/json", json);
}

static void serveApiAction() {
  String id = web.hasArg("id") ? web.arg("id") : "";
  String param = web.hasArg("p") ? web.arg("p") : "";
  if (id.length() == 0) { web.send(400, "text/plain", "missing id"); return; }
  bool ok = runAction(id, param);
  logEvent("action: %s%s%s -> %s", id.c_str(),
           param.length() ? "(" : "", param.c_str(),
           ok ? "ok" : "fail");
  String r = "{\"ok\":";
  r += ok ? "true" : "false";
  r += "}";
  web.send(200, "application/json", r);
}

static void serveApiMappings() {
  static const char* GIDS[] = {"1c", "2c", "3c", "4c", "hold"};
  String json = "{";
  for (size_t i = 0; i < 5; i++) {
    if (i) json += ',';
    GestureMap m = getMapping(GIDS[i]);
    String aid = m.actionId.length() ? m.actionId : defaultActionFor(GIDS[i]);
    bool isDefault = (m.actionId.length() == 0);
    json += "\""; json += GIDS[i]; json += "\":";
    json += "{\"action\":\""; json += aid;
    json += "\",\"param\":\""; json += m.param;
    json += "\",\"default\":"; json += isDefault ? "true" : "false";
    json += "}";
  }
  json += "}";
  web.send(200, "application/json", json);
}

static void serveApiSetMap() {
  if (!web.hasArg("g")) { web.send(400, "text/plain", "missing g"); return; }
  String g = web.arg("g");
  String a = web.hasArg("a") ? web.arg("a") : "";
  String p = web.hasArg("p") ? web.arg("p") : "";
  if (a.length() == 0 || a == "default") {
    clearMapping(g.c_str());
    logEvent("mapping %s reset to default", g.c_str());
  } else {
    setMapping(g.c_str(), a, p);
    logEvent("mapping %s -> %s", g.c_str(), a.c_str());
  }
  web.send(200, "application/json", "{\"ok\":true}");
}

// --- Control endpoints ---
static void serveApiVol() {
  if (!web.hasArg("v")) { web.send(400, "text/plain", "missing v"); return; }
  int v = web.arg("v").toInt();
  if (setVolume(v)) logEvent("vol -> %d (web)", v);
  web.send(200, "application/json", "{\"ok\":true}");
}

static void serveApiVolDelta() {
  int d = web.hasArg("d") ? web.arg("d").toInt() : 0;
  if (d == 0) { web.send(400, "text/plain", "missing d"); return; }
  int prev = spk.volume;
  int newVol = adjustVolume(d);
  logEvent("vol %d -> %d (web)", prev, newVol);
  web.send(200, "application/json", "{\"ok\":true}");
}

static void serveApiPlay() {
  togglePlay();
  logEvent("play/pause (web)");
  web.send(200, "application/json", "{\"ok\":true}");
}

static void serveApiNext() {
  nextTrack();
  logEvent("next (web)");
  web.send(200, "application/json", "{\"ok\":true}");
}

static void serveApiPrev() {
  prevTrack();
  logEvent("prev (web)");
  web.send(200, "application/json", "{\"ok\":true}");
}

static void serveApiMute() {
  bool ok = toggleMute();
  logEvent("mute -> %s%s", spk.muted ? "on" : "off", ok ? "" : " (FAILED)");
  String r = "{\"ok\":";
  r += ok ? "true" : "false";
  r += ",\"muted\":";
  r += spk.muted ? "true" : "false";
  r += "}";
  web.send(200, "application/json", r);
}

static void serveApiLog() {
  String out;
  out.reserve(LOG_LINES * 80);
  for (int i = 0; i < LOG_LINES; i++) {
    int idx = (logHead + i) % LOG_LINES;
    if (logRing[idx].length() > 0) {
      out += logRing[idx];
      out += '\n';
    }
  }
  web.send(200, "text/plain", out);
}

static void initWebUI(const char* hostname) {
  strncpy(deviceHostname, hostname, sizeof(deviceHostname));

  Preferences p;
  p.begin("sonos", true);
  deviceName = p.getString("devname", "");
  p.end();
  if (deviceName.length() == 0) deviceName = hostname;

  MDNS.begin(hostname);
  MDNS.addService("http", "tcp", 80);

  web.on("/", serveRoot);
  web.on("/logo.png", serveLogo);
  web.on("/api/status", serveApiStatus);
  web.on("/api/speakers", serveApiSpeakers);
  web.on("/api/select", serveApiSelect);
  web.on("/api/discover", serveApiDiscover);
  web.on("/api/scan", serveApiScan);
  web.on("/api/name", serveApiName);
  web.on("/api/reset", serveApiReset);
  web.on("/api/rooms", serveApiRooms);
  web.on("/api/setroom", serveApiSetRoom);
  web.on("/api/setprefix", serveApiSetPrefix);
  web.on("/api/setup", serveApiSetup);
  web.on("/api/restart", serveApiRestart);
  web.on("/api/setinvert", serveApiSetInvert);
  web.on("/api/setstep", serveApiSetStep);
  web.on("/api/checkupdate", serveApiCheckUpdate);
  web.on("/api/vol", serveApiVol);
  web.on("/api/voldelta", serveApiVolDelta);
  web.on("/api/play", serveApiPlay);
  web.on("/api/next", serveApiNext);
  web.on("/api/prev", serveApiPrev);
  web.on("/api/mute", serveApiMute);
  web.on("/api/actions", serveApiActions);
  web.on("/api/action", serveApiAction);
  web.on("/api/mappings", serveApiMappings);
  web.on("/api/setmap", serveApiSetMap);
  web.on("/api/bass", serveApiBass);
  web.on("/api/treble", serveApiTreble);
  web.on("/api/loudness", serveApiLoudness);
  web.on("/api/sound", serveApiSoundRefresh);
  web.on("/api/gesture", serveApiGesture);
  web.on("/api/log", serveApiLog);
  web.begin();
}
