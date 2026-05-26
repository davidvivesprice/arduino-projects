// Sonos Fleet Hub — Cloudflare Worker
//
// Receives periodic status reports from each Unit PoE-P4 in the field,
// stores them in KV, and serves a live dashboard. Each board POSTs every
// 5 min piggybacked on its manifest poll; this is what gives us at-a-glance
// visibility into the entire fleet across all installs.
//
// Endpoints:
//   POST /report      — board ingest (auth: X-Fleet-Auth header)
//   GET  /api/state   — JSON of every known board's latest status
//   GET  /            — embedded HTML dashboard

const DASHBOARD_HTML = `<!doctype html><html><head><meta charset="utf-8">
<title>TPS × Vives · Sonos Fleet</title>
<style>
:root{--bg:#0a0a0c;--ink:#e8e8ea;--ink2:#9c9ca0;--ink3:#6c6c70;--ink4:#2a2a2e;
  --ok:#5dd17b;--warn:#e0c25c;--bad:#ff5a5a;--accent:#8B0000;
  --serif:"Newsreader",Georgia,serif;--sans:-apple-system,Helvetica,Arial,sans-serif;
  --mono:ui-monospace,"SF Mono","Menlo",monospace}
*{box-sizing:border-box;margin:0;padding:0}
html,body{background:var(--bg);color:var(--ink);font-family:var(--sans);font-size:14px}
body{max-width:1100px;margin:0 auto;padding:36px 24px 60px;
  background:radial-gradient(900px 600px at 50% -10%,#14141c 0%,transparent 60%),
    linear-gradient(180deg,#0a0a0c 0%,#08080a 100%);min-height:100vh}
header{display:flex;flex-direction:column;align-items:center;border-bottom:1px solid var(--ink4);padding-bottom:18px;margin-bottom:28px}
header h1{font-family:var(--serif);font-weight:400;font-size:26pt;letter-spacing:-1px}
header .sub{font-family:var(--mono);font-size:10px;letter-spacing:3px;text-transform:uppercase;color:var(--ink3);margin-top:6px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:14px}
.card{background:#121214;border:1px solid var(--ink4);border-radius:8px;padding:16px;
  position:relative;transition:border-color .15s,transform .1s}
.card:hover{transform:translateY(-1px);border-color:var(--ink3)}
.card .room{font-family:var(--serif);font-size:18pt;letter-spacing:-.4px;line-height:1.05}
.card .host{font-family:var(--mono);font-size:10px;color:var(--accent);letter-spacing:1px;margin-top:4px}
.card .meta{display:grid;grid-template-columns:auto 1fr;gap:4px 10px;
  margin-top:12px;font-size:11.5px;color:var(--ink2)}
.card .meta b{color:var(--ink);font-weight:500;font-family:var(--mono);font-size:10.5px}
.card .badge{position:absolute;top:14px;right:14px;width:10px;height:10px;border-radius:50%}
.card.ok .badge{background:var(--ok);box-shadow:0 0 10px var(--ok)}
.card.warn .badge{background:var(--warn);box-shadow:0 0 10px var(--warn)}
.card.bad .badge{background:var(--bad);box-shadow:0 0 10px var(--bad)}
.card.bad{border-color:rgba(255,90,90,.4)}
.card.warn{border-color:rgba(224,194,92,.3)}
.card .err{margin-top:8px;font-size:11px;color:var(--bad);font-family:var(--mono)}
.legend{display:flex;gap:18px;justify-content:center;margin-top:24px;font-size:11px;color:var(--ink3);font-family:var(--mono);text-transform:uppercase;letter-spacing:1.5px}
.legend span{display:inline-flex;align-items:center;gap:6px}
.legend i{width:8px;height:8px;border-radius:50%;display:inline-block}
.legend .ok{background:var(--ok)}.legend .warn{background:var(--warn)}.legend .bad{background:var(--bad)}
.footer{text-align:center;margin-top:36px;font-size:10px;color:var(--ink3);font-family:var(--mono);letter-spacing:1.5px;text-transform:uppercase}
.empty{text-align:center;padding:60px 0;color:var(--ink3);font-family:var(--mono);text-transform:uppercase;letter-spacing:2px;font-size:11px}
.verdict{margin-bottom:20px;padding:14px 18px;border-radius:8px;font-family:var(--mono);font-size:12px;letter-spacing:1px;text-transform:uppercase;text-align:center;border:1px solid var(--ink4);background:#101012}
.verdict.allgreen{border-color:rgba(93,209,123,.35);color:var(--ok)}
.verdict.warning{border-color:rgba(224,194,92,.4);color:var(--warn)}
.verdict.danger{border-color:rgba(255,90,90,.4);color:var(--bad)}
.verdict .sub{display:block;color:var(--ink3);text-transform:none;letter-spacing:0;font-size:11px;margin-top:4px}
.pulse{margin-bottom:28px;background:#101012;border:1px solid var(--ink4);border-radius:8px;padding:14px 16px}
.pulse h2{font-family:var(--mono);font-size:10px;font-weight:600;letter-spacing:3px;text-transform:uppercase;color:var(--ink3);margin-bottom:10px;display:flex;justify-content:space-between;align-items:center}
.pulse h2 .live-dot{width:6px;height:6px;border-radius:50%;background:var(--ok);box-shadow:0 0 8px var(--ok);animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:0.4}}
.pulse .feed{max-height:280px;overflow-y:auto;font-family:var(--mono);font-size:11.5px;line-height:1.6}
.pulse .feed::-webkit-scrollbar{width:6px}
.pulse .feed::-webkit-scrollbar-thumb{background:var(--ink4);border-radius:3px}
.pulse .row{display:grid;grid-template-columns:60px 140px auto;gap:14px;padding:3px 0;border-bottom:1px solid #15151a}
.pulse .row:last-child{border-bottom:none}
.pulse .when{color:var(--ink3);text-align:right}
.pulse .who{color:var(--accent);font-weight:600}
.pulse .what{color:var(--ink)}
.pulse .what.fail{color:var(--bad)}
.pulse .feed-empty{color:var(--ink3);font-style:italic;padding:14px 0;text-align:center}
</style></head><body>
<header><h1>Sonos Fleet</h1><div class="sub">TPS × Vives · live</div></header>
<div id="verdict" class="verdict"></div>
<div class="pulse">
  <h2><span>Pulse · recent activity</span><span class="live-dot"></span></h2>
  <div id="pulseFeed" class="feed"><div class="feed-empty">no activity yet</div></div>
</div>
<div id="grid" class="grid"><div class="empty">loading…</div></div>
<div class="legend">
  <span><i class="ok"></i>online &lt;10m</span>
  <span><i class="warn"></i>stale 10–15m</span>
  <span><i class="bad"></i>down &gt;15m or unhealthy</span>
</div>
<div class="footer" id="lastpoll"></div>
<script>
const STALE=600, DEAD=900;  // seconds
async function load(){
  try{
    const [r1,r2]=await Promise.all([
      fetch('/api/state',{cache:'no-store'}),
      fetch('/api/pulse',{cache:'no-store'})
    ]);
    const s=await r1.json();
    const p=await r2.json();
    render(s);
    renderPulse(p);
  }catch(e){
    document.getElementById('grid').innerHTML='<div class="empty">hub unreachable</div>';
  }
}
const GLABEL={'1c':'tap','2c':'double-tap','3c':'triple-tap','4c':'quad-tap','5c':'5× tap','hold':'hold','lh':'long hold','1c+h':'tap + hold','2c+h':'double + hold','3c+h':'triple + hold','4c+h':'quad + hold'};
function labelFor(gid){
  // Rotation bursts: "rot+5", "rot-12" → "vol up · medium (+5)" etc.
  const m = String(gid).match(/^rot([+-])(\\d+)$/);
  if(m){
    const dir=m[1]==='+'?'up':'down';
    const n=parseInt(m[2],10);
    const speed = n>=10?'fast spin':n>=4?'medium':'light';
    return \`vol \${dir} · \${speed} (\${m[1]}\${n})\`;
  }
  return GLABEL[gid]||gid;
}
function renderPulse(p){
  const feed=document.getElementById('pulseFeed');
  const events=p.events||[];
  if(events.length===0){feed.innerHTML='<div class="feed-empty">no gestures yet</div>';return;}
  const now=Date.now()/1000;
  let h='';
  for(const e of events){
    const since=Math.max(0,Math.floor(now-e.t));
    const when=since<60?since+'s':since<3600?Math.floor(since/60)+'m':Math.floor(since/3600)+'h';
    const label=labelFor(e.gid);
    const okCls=e.ok?'':' fail';
    const tail=e.ok?'':' · didn\\'t fire';
    h+=\`<div class="row"><div class="when">\${when}</div>
      <div class="who">\${esc(e.id.replace(/^(tpsvc|phil)-/,''))}</div>
      <div class="what\${okCls}">\${esc(label)}\${tail}</div></div>\`;
  }
  feed.innerHTML=h;
}
function ago(sec){
  if(sec<60) return sec+'s';
  if(sec<3600) return Math.floor(sec/60)+'m';
  if(sec<86400) return Math.floor(sec/3600)+'h '+Math.floor((sec%3600)/60)+'m';
  return Math.floor(sec/86400)+'d';
}
function render(j){
  const now=Date.now()/1000;
  const boards=j.boards||[];
  const v=document.getElementById('verdict');
  if(boards.length===0){
    document.getElementById('grid').innerHTML='<div class="empty">no boards have reported yet</div>';
    if(v){v.className='verdict warning';v.innerHTML='Waiting for first report…<span class="sub">Boards report on boot + every 5 min.</span>';}
    return;
  }
  // Up-front health verdict: how many ok, how many sick, what's wrong?
  let okCount=0,sick=[];
  for(const b of boards){
    const s=stateOf(b,now);
    if(s==='ok'){okCount++;continue;}
    const why=[];
    if((now-b.ts)>DEAD) why.push('offline');
    if(!b.i2c) why.push('no encoder');
    if(!b.spkOnline) why.push('no speaker');
    if(s==='warn'&&why.length===0) why.push('stale report');
    sick.push({id:b.id,label:b.label||b.room||b.id,why:why.join(', ')});
  }
  if(v){
    if(sick.length===0){
      v.className='verdict allgreen';
      v.innerHTML=\`All \${boards.length} boards healthy<span class="sub">Encoders detected, Sonos online, reporting normally.</span>\`;
    } else {
      const cls=sick.some(s=>s.why.includes('offline'))?'danger':'warning';
      v.className='verdict '+cls;
      const list=sick.map(s=>\`\${esc(s.label)}: \${esc(s.why)}\`).join(' · ');
      v.innerHTML=\`\${okCount} of \${boards.length} healthy<span class="sub">\${list}</span>\`;
    }
  }
  // Sort: bad first, then warn, then ok by client+room
  boards.sort((a,b)=>{
    const order={bad:0,warn:1,ok:2};
    return order[stateOf(a,now)]-order[stateOf(b,now)] || (a.client||'').localeCompare(b.client||'') || (a.room||'').localeCompare(b.room||'');
  });
  let h='';
  for(const b of boards){
    const s=stateOf(b,now);
    const lastSeen=Math.floor(now-b.ts);
    const errs=Object.entries(b.errors24h||{}).filter(([_,n])=>n>0).map(([k,n])=>k+':'+n).join(' ');
    h+=\`<div class="card \${s}"><div class="badge"></div>
      <div class="room">\${esc(b.label||b.room||b.id)}</div>
      <div class="host">\${esc(b.id)}</div>
      <div class="meta">
        <span>last seen</span><b>\${ago(lastSeen)} ago</b>
        <span>uptime</span><b>\${ago(b.up||0)}</b>
        <span>fw</span><b>\${esc(b.fw||'?')}</b>
        <span>i2c</span><b>\${b.i2c?'✓ 0x'+(b.ss_pid||'').toString():'✗ none'}</b>
        <span>speaker</span><b>\${b.speaker?(b.spkOnline?'✓ '+esc(b.speaker):'✗ '+esc(b.speaker)):'—'}</b>
        <span>rotations</span><b>\${b.rotEvents??0} total</b>
        <span>last knob</span><b>\${lastTouchedAgo(b,now)}</b>
      </div>\${errs?'<div class="err">'+esc(errs)+'</div>':''}
    </div>\`;
  }
  document.getElementById('grid').innerHTML=h;
  document.getElementById('lastpoll').textContent='last refresh '+new Date().toLocaleTimeString();
}
function lastTouchedAgo(b,now){
  // Convert board millis() lastRotMs into a wall-clock ago via the same
  // (nowMs - lastRotMs)/1000 offset trick used server-side for events.
  if(!b.lastRotMs || !b.nowMs) return '—';
  const ageMs = b.nowMs - b.lastRotMs;
  if(ageMs<0) return '—';
  const ageS = Math.floor(ageMs/1000) + Math.floor(now - b.ts);
  if(ageS<60) return ageS+'s ago';
  if(ageS<3600) return Math.floor(ageS/60)+'m ago';
  return ago(ageS)+' ago';
}
function stateOf(b,now){
  const since=now-b.ts;
  if(since>DEAD) return 'bad';
  if(!b.i2c || !b.spkOnline) return 'bad';
  if(since>STALE) return 'warn';
  return 'ok';
}
function esc(s){return String(s||'').replace(/[<>&"']/g,c=>({'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;',"'":'&#39;'}[c]));}
load();
setInterval(load, 15000);
</script></body></html>`;

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const path = url.pathname;

    if (request.method === "POST" && path === "/report") {
      return handleReport(request, env);
    }
    if (request.method === "GET" && path === "/api/state") {
      return handleState(env);
    }
    if (request.method === "GET" && path === "/api/pulse") {
      return handlePulse(env);
    }
    if (request.method === "GET" && (path === "/" || path === "/index.html")) {
      return new Response(DASHBOARD_HTML, {
        headers: { "content-type": "text/html; charset=utf-8", "cache-control": "no-store" },
      });
    }
    return new Response("Not found", { status: 404 });
  },
};

async function handleReport(request, env) {
  // Auth: shared secret in X-Fleet-Auth header. Boards have this baked into
  // firmware; rotating means reflashing the fleet, so treat it as semi-stable.
  const auth = request.headers.get("X-Fleet-Auth") || "";
  if (auth !== env.INGEST_SECRET) {
    return new Response(JSON.stringify({ ok: false, err: "auth" }), {
      status: 401, headers: { "content-type": "application/json" },
    });
  }

  let body;
  try { body = await request.json(); }
  catch { return new Response('{"ok":false,"err":"bad json"}', { status: 400, headers: { "content-type": "application/json" } }); }

  const id = String(body.id || "").slice(0, 64);
  if (!id || !/^[a-z0-9-]+$/.test(id)) {
    return new Response('{"ok":false,"err":"bad id"}', { status: 400, headers: { "content-type": "application/json" } });
  }

  // Stamp ingest time so dashboard can compute "last seen".
  const nowSec = Math.floor(Date.now() / 1000);
  const record = { ...body, ts: nowSec };
  const ttl = parseInt(env.LATEST_TTL_SECONDS || "1800", 10);
  await env.STATE.put(`latest:${id}`, JSON.stringify(record), {
    expirationTtl: ttl,
  });

  // Pulse feed — append individual gesture events. Convert board millis() to
  // an absolute wall-clock guess using (now - (nowMs - eventMs)/1000), so the
  // dashboard can sort across boards even though their clocks aren't sync'd.
  const incoming = Array.isArray(body.events) ? body.events : [];
  const nowMs = Number(body.nowMs) || 0;
  if (incoming.length > 0 && nowMs > 0) {
    const eventsKey = `events:${id}`;
    let log = [];
    try {
      const prev = await env.STATE.get(eventsKey);
      if (prev) log = JSON.parse(prev);
    } catch {}
    for (const e of incoming) {
      const ageMs = nowMs - Number(e.ms || 0);
      if (ageMs < 0 || ageMs > 24 * 3600 * 1000) continue;  // sanity
      log.push({
        t:   nowSec - Math.floor(ageMs / 1000),
        gid: String(e.gid || "").slice(0, 8),
        ok:  !!e.ok,
        id,
      });
    }
    // Keep only events from the last 24h, cap at 200 per board.
    const cutoff = nowSec - 24 * 3600;
    log = log.filter(e => e.t >= cutoff).slice(-200);
    await env.STATE.put(eventsKey, JSON.stringify(log), {
      expirationTtl: 25 * 3600,  // a touch over the 24h window
    });
  }

  return new Response(JSON.stringify({ ok: true }), {
    headers: { "content-type": "application/json", "cache-control": "no-store" },
  });
}

async function handlePulse(env) {
  // Merge all per-board event logs into one chronological feed.
  const list = await env.STATE.list({ prefix: "events:" });
  const all = [];
  for (const key of list.keys) {
    const raw = await env.STATE.get(key.name);
    if (!raw) continue;
    try {
      const log = JSON.parse(raw);
      for (const e of log) all.push(e);
    } catch {}
  }
  all.sort((a, b) => b.t - a.t);  // newest first
  return new Response(JSON.stringify({ events: all.slice(0, 100) }), {
    headers: { "content-type": "application/json", "cache-control": "no-store" },
  });
}

async function handleState(env) {
  // List all latest:<id> keys, fetch each. Free-tier KV list is fine for our
  // scale (we'd need >1000 keys to start paginating).
  const list = await env.STATE.list({ prefix: "latest:" });
  const boards = [];
  for (const key of list.keys) {
    const raw = await env.STATE.get(key.name);
    if (raw) {
      try { boards.push(JSON.parse(raw)); }
      catch {}
    }
  }
  return new Response(JSON.stringify({ boards, generated: Date.now() }), {
    headers: { "content-type": "application/json", "cache-control": "no-store" },
  });
}
