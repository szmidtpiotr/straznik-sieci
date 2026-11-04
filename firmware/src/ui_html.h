// Panel page (single file, no external assets). Design notes: .interface-design/system.md
#pragma once

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#0b0d10"><meta name="color-scheme" content="dark">
<title>Strażnik sieci</title>
<style>
:root{
 /* rack at night: anodised black, status LEDs, silkscreen legends */
 --rack:#0b0d10;--panel:#101318;--bezel:#161a20;--bezel-up:#1c2129;--well:#07080b;
 --seam:rgba(200,215,235,.07);--seam-soft:rgba(200,215,235,.045);--seam-strong:rgba(200,215,235,.14);
 --label:#e7ecf2;--legend:#a9b3bf;--silk:#6c7784;--off:#434b56;
 --link:#3ddc84;--act:#f2a93b;--fault:#ff5a52;--ident:#4aa3ff;
 --glow-link:0 0 6px rgba(61,220,132,.55);--glow-act:0 0 7px rgba(242,169,59,.6);--glow-fault:0 0 8px rgba(255,90,82,.7);--glow-ident:0 0 7px rgba(74,163,255,.6);
 --ui:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
 --mono:ui-monospace,"SF Mono","JetBrains Mono",Menlo,Consolas,monospace;
 --r-sm:5px;--r:8px;--nav-h:60px;
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html{background:var(--rack)}
body{margin:0;font:15px/1.45 var(--ui);color:var(--label);background:var(--rack);padding-bottom:calc(var(--nav-h) + env(safe-area-inset-bottom) + 16px)}
.mono{font-family:var(--mono);font-variant-numeric:tabular-nums}
.silk{font-size:11px;letter-spacing:.09em;text-transform:uppercase;color:var(--silk);font-weight:600}
button{font:inherit;color:inherit;cursor:pointer;border:1px solid var(--seam-strong);background:var(--bezel);border-radius:var(--r-sm);padding:9px 14px;min-height:40px;transition:background .12s ease-out,border-color .12s}
button:hover{background:var(--bezel-up)}button:active{transform:translateY(1px)}
button:focus-visible,input:focus-visible,select:focus-visible,textarea:focus-visible,summary:focus-visible{outline:2px solid var(--ident);outline-offset:1px}
button.pri{background:#12304f;border-color:rgba(74,163,255,.45);color:#d6e9ff}button.pri:hover{background:#163a60}
button.ghost{background:transparent;border-color:transparent;color:var(--legend)}button.ghost:hover{background:var(--bezel)}
button.danger{background:#3a1413;border-color:rgba(255,90,82,.45);color:#ffd9d6}
button:disabled{opacity:.45;cursor:default}
input,select,textarea{font:inherit;color:var(--label);background:var(--well);border:1px solid var(--seam-strong);border-radius:var(--r-sm);padding:9px 10px;min-height:40px;width:100%}
select{appearance:none;background-image:linear-gradient(45deg,transparent 50%,var(--silk) 50%),linear-gradient(135deg,var(--silk) 50%,transparent 50%);background-position:calc(100% - 16px) 55%,calc(100% - 11px) 55%;background-size:5px 5px;background-repeat:no-repeat;padding-right:30px}
textarea{font:12px/1.45 var(--mono);min-height:320px}
label.f{display:grid;gap:5px;font-size:13px;color:var(--legend)}
label.f small{color:var(--silk);font-size:12px}

/* ---------- top: brand, LED strip, state line ---------- */
.top{position:sticky;top:0;z-index:5;background:var(--rack);border-bottom:1px solid var(--seam);padding:calc(10px + env(safe-area-inset-top)) 16px 10px}
.brand{display:flex;align-items:center;gap:10px}
.brand b{font-size:13px;letter-spacing:.14em;font-weight:700}
.brand .grow{flex:1}
.strip{display:flex;flex-wrap:wrap;gap:5px;margin:10px 0 8px}
.led{width:9px;height:9px;border-radius:50%;background:var(--off);flex:none;display:inline-block}
.strip .led{width:11px;height:11px;border-radius:2px;cursor:pointer}
.led.up{background:var(--link);box-shadow:var(--glow-link)}
.led.warn{background:var(--act);box-shadow:var(--glow-act)}
.led.down{background:var(--fault);box-shadow:var(--glow-fault);animation:blink 1.2s steps(2,start) infinite}
.led.sup{background:#7a2e2b}
.led.ident{background:var(--ident);box-shadow:var(--glow-ident)}
.led.scan{outline:2px solid rgba(231,236,242,.85);outline-offset:1px}
@keyframes blink{to{opacity:.35}}
.state{display:flex;align-items:baseline;gap:10px;flex-wrap:wrap}
.state .headline{font-weight:650;font-size:16px}
.state .sub{color:var(--silk);font-size:12.5px}
.state.ok .headline{color:var(--label)}
.state.warn .headline{color:var(--act)}
.state.bad .headline{color:var(--fault)}
.chips{display:flex;gap:6px;flex-wrap:wrap;margin-top:6px}
.chip{font-size:12px;padding:2px 8px;border-radius:999px;border:1px solid var(--seam-strong);color:var(--legend)}
.chip.act{color:var(--act);border-color:rgba(242,169,59,.4)}.chip.fault{color:var(--fault);border-color:rgba(255,90,82,.45)}.chip.ident{color:var(--ident);border-color:rgba(74,163,255,.4)}

/* fault banner */
.alarm{margin:12px 16px 0;border:1px solid rgba(255,90,82,.45);background:#1a0f10;border-radius:var(--r);padding:12px}
.alarm .item{display:flex;gap:10px;align-items:center;padding:6px 0}
.alarm .item+.item{border-top:1px solid rgba(255,90,82,.15)}
.alarm .actions{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}

/* ---------- views ---------- */
main{max-width:760px;margin:0 auto;padding:0 0 8px}
.view{display:none;padding:14px 16px 0}.view.on{display:block}
.bar{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:12px}
.bar .grow{flex:1}
.card{background:var(--panel);border:1px solid var(--seam);border-radius:var(--r)}
.card+.card{margin-top:12px}
.pad{padding:14px}

/* tree */
.tree{background:var(--panel);border:1px solid var(--seam);border-radius:var(--r);overflow:hidden}
.sec{display:flex;align-items:center;gap:8px;padding:12px 14px 6px;cursor:pointer;user-select:none}
.sec+.row,.row+.sec{border-top:1px solid var(--seam)}
.row{display:grid;grid-template-columns:auto auto 1fr auto;align-items:center;gap:0 10px;padding:10px 14px;min-height:52px;cursor:pointer;border-top:1px solid var(--seam-soft)}
.row:first-child{border-top:0}
.row:hover{background:var(--bezel)}
.row.flash{animation:flash 1.4s ease-out}
@keyframes flash{from{background:#1b2a3d}to{background:transparent}}
.row.off{opacity:.45}
.row .ind{font:12px var(--mono);color:#2c333c;white-space:pre;display:flex;align-items:center}
.tw{width:18px;display:inline-grid;place-items:center;color:var(--silk);font-size:11px}
.row .nm{min-width:0}
.row .nm b{font-weight:550;display:block;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.row .nm small{display:block;color:var(--silk);font:12px var(--mono);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.row .val{text-align:right;font:13px var(--mono);color:var(--legend);white-space:nowrap}
.row .val.bad{color:var(--fault)}.row .val.warn{color:var(--act)}
.badge{font:11px var(--ui);color:var(--silk);margin-left:6px}
.badge.bad{color:var(--fault)}
.muted-ico{color:var(--silk);font-size:12px;margin-left:6px}

/* server */
.kv{display:grid;grid-template-columns:auto 1fr;gap:10px 16px;align-items:center}
.kv dt{color:var(--silk);font-size:13px}.kv dd{margin:0;text-align:right}
.power{display:flex;align-items:center;gap:12px}
.power .big{font-size:18px;font-weight:650}
.hold{position:relative;overflow:hidden;width:100%;min-height:54px;font-weight:650;letter-spacing:.02em;touch-action:none;user-select:none;-webkit-user-select:none}
.hold::before{content:"";position:absolute;inset:0;width:calc(var(--p,0)*100%);background:rgba(255,90,82,.35);transition:width .05s linear}
.hold span{position:relative}
.note{color:var(--silk);font-size:12.5px;margin:10px 0 0}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px}

/* log */
.seg{display:flex;background:var(--well);border:1px solid var(--seam);border-radius:var(--r);padding:3px;gap:3px;overflow-x:auto}
.seg button{flex:1;min-height:34px;padding:6px 10px;border:0;background:transparent;color:var(--legend);white-space:nowrap;font-size:13.5px}
.seg button.on{background:var(--bezel-up);color:var(--label)}
.log{margin-top:12px}
.le{display:grid;grid-template-columns:56px 1fr;gap:10px;padding:9px 14px;border-top:1px solid var(--seam-soft);font-size:14px}
.le:first-child{border-top:0}
.le time{font:12px var(--mono);color:var(--silk);padding-top:2px}
.le .msg{white-space:pre-wrap;word-break:break-word}
.le .tg{display:flex;gap:6px;align-items:center;font-size:11.5px;color:var(--silk);margin-bottom:2px}
.day{padding:10px 14px 4px}
.empty{padding:28px 14px;text-align:center;color:var(--silk)}

/* settings */
details.set{background:var(--panel);border:1px solid var(--seam);border-radius:var(--r)}
details.set+details.set{margin-top:10px}
details.set summary{list-style:none;cursor:pointer;padding:14px;display:flex;align-items:center;gap:10px}
details.set summary::-webkit-details-marker{display:none}
details.set summary::after{content:"›";margin-left:auto;color:var(--silk);transform:rotate(90deg);transition:transform .15s}
details.set[open] summary::after{transform:rotate(-90deg)}
details.set summary b{font-weight:600}
details.set summary small{color:var(--silk);font-size:12.5px;display:block}
details.set .body{padding:0 14px 14px;display:grid;gap:12px}
.fields{display:grid;gap:12px;grid-template-columns:1fr}
@media(min-width:560px){.fields{grid-template-columns:1fr 1fr}}
.save{display:flex;gap:10px;align-items:center}
.ok-msg{color:var(--link);font-size:13px}.err-msg{color:var(--fault);font-size:13px}
table.ref{width:100%;border-collapse:collapse;font-size:12.5px}
table.ref td{padding:6px 4px;border-top:1px solid var(--seam-soft);vertical-align:top;color:var(--legend)}
table.ref code{color:var(--ident);font-family:var(--mono)}

/* bottom sheet */
.scrim{position:fixed;inset:0;background:rgba(0,0,0,.55);z-index:20;display:none}
.sheet{position:fixed;left:0;right:0;bottom:0;z-index:21;background:var(--panel);border-top:1px solid var(--seam-strong);border-radius:14px 14px 0 0;max-height:88vh;overflow:auto;padding:8px 16px calc(18px + env(safe-area-inset-bottom));transform:translateY(105%);transition:transform .2s cubic-bezier(.2,.8,.2,1)}
.sheet{visibility:hidden;pointer-events:none;transition:transform .2s cubic-bezier(.2,.8,.2,1),visibility 0s linear .2s}
.sheet.on{transform:none;visibility:visible;pointer-events:auto;transition:transform .2s cubic-bezier(.2,.8,.2,1)}.scrim.on{display:block}
.grab{width:38px;height:4px;border-radius:2px;background:var(--seam-strong);margin:4px auto 12px}
@media(min-width:760px){.sheet{left:50%;right:auto;width:600px;transform:translate(-50%,105%);bottom:auto;top:8vh;border-radius:12px;border:1px solid var(--seam-strong)}.sheet.on{transform:translate(-50%,0)}}
.spark{width:100%;height:64px;display:block;background:var(--well);border:1px solid var(--seam);border-radius:var(--r-sm)}
.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-top:8px}
.stats div{background:var(--well);border:1px solid var(--seam);border-radius:var(--r-sm);padding:8px}
.stats b{display:block;font:15px var(--mono)}
.stats span{font-size:11.5px;color:var(--silk)}
.acts{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:14px}
.types{display:grid;grid-template-columns:repeat(4,1fr);gap:6px}
.types button{min-height:38px;padding:6px}
.types button.on{background:#12304f;border-color:rgba(74,163,255,.5)}

/* bottom nav */
nav.tabs{position:fixed;left:0;right:0;bottom:0;z-index:10;display:grid;grid-template-columns:repeat(4,1fr);background:rgba(11,13,16,.96);border-top:1px solid var(--seam);padding-bottom:env(safe-area-inset-bottom);backdrop-filter:blur(8px)}
nav.tabs button{border:0;background:transparent;border-radius:0;min-height:var(--nav-h);display:grid;place-items:center;gap:2px;color:var(--silk);font-size:11.5px;padding:6px 2px;position:relative}
nav.tabs button svg{width:22px;height:22px;stroke:currentColor;fill:none;stroke-width:1.7}
nav.tabs button.on{color:var(--label)}
nav.tabs button.on::before{content:"";position:absolute;top:0;left:30%;right:30%;height:2px;background:var(--ident);border-radius:0 0 2px 2px}
nav.tabs .dot{position:absolute;top:9px;right:calc(50% - 18px);width:7px;height:7px;border-radius:50%}
@media(min-width:900px){
 body{padding-bottom:24px}
 nav.tabs{position:sticky;top:0;bottom:auto;max-width:760px;margin:0 auto;border:1px solid var(--seam);border-radius:var(--r);grid-template-columns:repeat(4,auto);justify-content:start;background:var(--panel);margin-top:12px}
 nav.tabs button{grid-auto-flow:column;min-height:44px;padding:0 18px;gap:8px;font-size:13.5px}
 nav.tabs button.on::before{top:auto;bottom:0;left:18px;right:18px}
 .top{position:static}.top .in{max-width:760px;margin:0 auto}
}
.stale main,.stale .strip{opacity:.5}
</style></head><body>

<header class="top"><div class="in">
 <div class="brand"><span class="led" id="selfLed"></span><b>STRAŻNIK SIECI</b><span class="grow"></span><span class="silk mono" id="clock">--:--</span></div>
 <div class="strip" id="strip" aria-label="Diody posterunków"></div>
 <div class="state" id="state"><span class="headline">Łączę ze strażnikiem…</span></div>
 <div class="chips" id="chips"></div>
</div></header>
<div id="alarm"></div>

<nav class="tabs" id="tabs">
 <button data-v="cele" class="on"><svg viewBox="0 0 24 24"><rect x="3" y="4" width="18" height="5" rx="1"/><rect x="3" y="15" width="18" height="5" rx="1"/><path d="M7 6.5h.01M7 17.5h.01"/></svg>Posterunki</button>
 <button data-v="serwer"><svg viewBox="0 0 24 24"><path d="M12 3v8"/><path d="M6.4 7.4a8 8 0 1 0 11.2 0"/></svg>Serwer<span class="dot" id="srvDot"></span></button>
 <button data-v="dziennik"><svg viewBox="0 0 24 24"><path d="M5 4h11l3 3v13H5z"/><path d="M9 10h7M9 14h7M9 18h4"/></svg>Dziennik</button>
 <button data-v="ustawienia"><svg viewBox="0 0 24 24"><path d="M4 7h10M18 7h2M4 17h4M12 17h8"/><circle cx="16" cy="7" r="2"/><circle cx="10" cy="17" r="2"/></svg>Ustawienia</button>
</nav>

<main>
<!-- ================= POSTERUNKI ================= -->
<section class="view on" data-view="cele">
 <div class="bar">
  <button class="pri" onclick="act('sweep')">Sprawdź teraz</button>
  <select id="muteSel" style="width:auto" onchange="muteAll(this)">
   <option value="">Wycisz wszystko…</option><option value="30">na 30 min</option><option value="60">na 1 h</option><option value="240">na 4 h</option><option value="1440">na 24 h</option><option value="-1">do odwołania</option><option value="0">Odcisz wszystko</option>
  </select>
  <span class="grow"></span>
  <span style="display:flex;gap:4px;margin-left:auto"><button class="ghost" onclick="setAll(false)" title="Rozwiń wszystko" aria-label="Rozwiń wszystko">▾▾</button><button class="ghost" onclick="setAll(true)" title="Zwiń wszystko" aria-label="Zwiń wszystko">▸▸</button>
  <button onclick="openForm()">+ Dodaj</button></span>
 </div>
 <div class="tree" id="tree"><div class="empty">Wczytuję…</div></div>
</section>

<!-- ================= SERWER ================= -->
<section class="view" data-view="serwer">
 <div class="card pad">
  <div class="power"><span class="led" id="pwLed" style="width:14px;height:14px"></span><div><div class="big" id="pwName">Serwer</div><div id="pwText" style="color:var(--legend);font-size:13.5px">zasilanie: —</div></div></div>
  <dl class="kv" style="margin:16px 0 0">
   <dt>Tryb restartu</dt><dd id="srvMode">—</dd>
   <dt>Automat</dt><dd id="srvAuto">—</dd>
   <dt>Procedura</dt><dd id="srvState">—</dd>
   <dt>BMC</dt><dd class="mono" id="srvBmc">—</dd>
  </dl>
 </div>
 <div class="card pad">
  <button class="hold danger" id="holdReset" data-label="Przytrzymaj 2 s — twardy reset serwera"><span>Przytrzymaj 2 s — twardy reset serwera</span></button>
  <p class="note" id="holdNote">Działa jak przycisk RESET na obudowie: na chwilę padnie wszystko, co działa na serwerze.</p>
  <div class="grid2" style="margin-top:12px">
   <button onclick="ipmi('status')">Sprawdź zasilanie</button>
   <button onclick="if(confirm('Włączyć zasilanie serwera?'))ipmi('on')">Włącz zasilanie</button>
  </div>
  <div class="grid2" style="margin-top:8px" id="simBox">
   <button onclick="ipmi('simulate&on=1')">🧪 Symuluj awarię</button>
   <button class="ghost" onclick="ipmi('simulate&on=0')">Zakończ symulację</button>
  </div>
  <p class="note" id="ipmiMsg"></p>
 </div>
 <p class="note" style="padding:0 2px">Gdy serwer nie odpowiada od ustawionego czasu, a IPMI mówi „zasilanie włączone”, w Telegramie przychodzi pytanie
 Zrestartuj / Czekaj / Nie restartuj. Automat resetuje sam tylko w swoim oknie godzinowym, najwyżej raz na 2 h, i nie próbuje drugi raz,
 jeśli serwer nie wstanie w 15 min. Gdy nie odpowiada router, strażnik nigdy nie resetuje na ślepo. Ustawienia → Restart serwera.</p>
</section>

<!-- ================= DZIENNIK ================= -->
<section class="view" data-view="dziennik">
 <div class="seg" id="logSeg">
  <button data-f="all" class="on">Wszystko</button><button data-f="fault">Awarie</button><button data-f="tg">Telegram</button><button data-f="sys">System</button>
 </div>
 <div class="card log" id="log"><div class="empty">Wczytuję…</div></div>
</section>

<!-- ================= USTAWIENIA ================= -->
<section class="view" data-view="ustawienia">
 <div id="pending"></div>
 <details class="set" open><summary><div><b>Sprawdzanie</b><small>jak często i kiedy uznać, że coś leży</small></div></summary>
  <form class="body" data-save><div class="fields">
   <label class="f">Co ile sekund obchód<input name="interval" type="number" min="10" max="3600" inputmode="numeric"></label>
   <label class="f">Porażek pod rząd = awaria<input name="faillimit" type="number" min="1" max="20" inputmode="numeric"></label>
   <label class="f">Przypomnienie co … min<small>0 = bez przypomnień</small><input name="remind" type="number" min="0" max="1440" inputmode="numeric"></label>
   <label class="f">Poranne „żyję” o godzinie<small>-1 = wyłączone</small><input name="hb" type="number" min="-1" max="23" inputmode="numeric"></label>
  </div><div class="save"><button class="pri">Zapisz</button><span class="msg"></span></div></form>
 </details>
 <details class="set"><summary><div><b>Telegram</b><small id="tgSum">bot i czat</small></div></summary>
  <form class="body" data-save><div class="fields">
   <label class="f">Token bota<small>puste = bez zmian</small><input name="tgtoken" type="password" autocomplete="off"></label>
   <label class="f">Czat (ID)<input name="tgchat" inputmode="numeric"></label>
  </div><div class="save"><button class="pri">Zapisz</button><button type="button" onclick="act('test');flash(this,'wysłano')">Wyślij test</button><span class="msg"></span></div></form>
 </details>
 <details class="set"><summary><div><b>Restart serwera</b><small id="rsSum">IPMI i automat</small></div></summary>
  <form class="body" data-save><div class="fields">
   <label class="f">Tryb<select name="rdry"><option value="1">🧪 testowy — wszystko oprócz resetu</option><option value="0">⚠️ prawdziwy — reset idzie do BMC</option></select></label>
   <label class="f">Automat<select name="rauto"><option value="0">wyłączony — tylko pytam</option><option value="1">włączony — resetuję sam w oknie</option></select></label>
   <label class="f">Okno automatu od<input name="rwins" type="time"></label>
   <label class="f">Okno automatu do<input name="rwine" type="time"></label>
   <label class="f">Reset po … min bez odpowiedzi<input name="rautomin" type="number" min="5" max="1440" inputmode="numeric"></label>
   <label class="f">Pytaj po … min awarii<input name="rhung" type="number" min="1" max="240" inputmode="numeric"></label>
   <label class="f">Cel = serwer (id)<input name="rtgt"></label>
   <label class="f">BMC (IPMI) adres<input name="iphost"></label>
   <label class="f">IPMI użytkownik<input name="ipuser" autocomplete="off"></label>
   <label class="f">IPMI hasło<small>puste = bez zmian</small><input name="ippass" type="password" autocomplete="off"></label>
  </div><div class="save"><button class="pri">Zapisz</button><span class="msg"></span></div></form>
 </details>
 <details class="set"><summary><div><b>Home Assistant (MQTT)</b><small id="mqSum">broker</small></div></summary>
  <form class="body" data-save><div class="fields">
   <label class="f">Broker<small>puste = MQTT wyłączone</small><input name="mqhost"></label>
   <label class="f">Port<input name="mqport" type="number" min="1" max="65535" inputmode="numeric"></label>
   <label class="f">Użytkownik<input name="mquser" autocomplete="off"></label>
   <label class="f">Hasło<small>puste = bez zmian</small><input name="mqpass" type="password" autocomplete="off"></label>
  </div><div class="save"><button class="pri">Zapisz</button><span class="msg"></span></div></form>
 </details>
 <details class="set"><summary><div><b>Wi-Fi</b><small id="wfSum">sieć</small></div></summary>
  <form class="body" data-save><div class="fields">
   <label class="f">Sieć (SSID)<input name="ssid" autocomplete="off"></label>
   <label class="f">Hasło<small>puste = bez zmian</small><input name="pass" type="password" autocomplete="off"></label>
  </div><p class="note">Zmiana sieci restartuje strażnika. Jeśli nie połączy się w minutę, wystawi sieć „Straznik-Setup” z panelem pod 192.168.4.1.</p>
  <div class="save"><button class="pri">Zapisz</button><span class="msg"></span></div></form>
 </details>
 <details class="set"><summary><div><b>Lista celów — edytor JSON</b><small>dla zaawansowanych; zwykle wystarczy „+ Dodaj”</small></div></summary>
  <div class="body">
   <textarea id="tj" spellcheck="false"></textarea>
   <div class="save"><button class="pri" onclick="saveJson()">Zapisz listę</button><button class="ghost" onclick="loadJson()">Wczytaj ponownie</button><span class="msg" id="tjmsg"></span></div>
   <table class="ref"><tbody>
    <tr><td><code>id</code></td><td>krótki identyfikator bez spacji — używany w <code>parent</code> i w Telegramie</td></tr>
    <tr><td><code>name</code></td><td>nazwa w panelu i wiadomościach</td></tr>
    <tr><td><code>type</code></td><td><code>ping</code> · <code>tcp</code> · <code>http</code> · <code>dns</code> · <code>separator</code> (nagłówek sekcji)</td></tr>
    <tr><td><code>host</code></td><td>adres IP albo nazwa</td></tr>
    <tr><td><code>port</code></td><td>tcp: wymagany; http: domyślnie 80 (443 z <code>tls</code>)</td></tr>
    <tr><td><code>path</code> · <code>expect</code> · <code>tls</code></td><td>http: ścieżka, wymagany kod (0 = każdy poniżej 500), https bez sprawdzania certyfikatu</td></tr>
    <tr><td><code>query</code></td><td>dns: jaką nazwę rozwiązać</td></tr>
    <tr><td><code>parent</code></td><td>id celu, bez którego ten nie działa (musi stać wyżej)</td></tr>
    <tr><td><code>enabled</code></td><td><code>false</code> = na liście, ale niesprawdzany</td></tr>
   </tbody></table>
  </div>
 </details>
 <details class="set"><summary><div><b>System</b><small id="sysSum">firmware, pamięć, aktualizacja</small></div></summary>
  <div class="body">
   <dl class="kv" id="sysKv"></dl>
   <div class="grid2"><a href="/update"><button style="width:100%">Aktualizacja (OTA)</button></a>
   <button class="danger" onclick="if(confirm('Zrestartować strażnika? Przez ok. 20 s nic nie będzie pilnowane.'))act('reboot')">Restart strażnika</button></div>
  </div>
 </details>
</section>
</main>

<!-- ================= SHEET (target card / form) ================= -->
<div class="scrim" id="scrim" onclick="closeSheet()"></div>
<div class="sheet" id="sheet" role="dialog" aria-modal="true"><div class="grab"></div><div id="sheetBody"></div></div>

<script>
const $=id=>document.getElementById(id);
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const pad=n=>String(n).padStart(2,'0');
const FOREVER=2147000000;
let S=null,lastOk=0,view='cele',logFilter='all',formsFilled=false,sheetTarget=null;
const collapsed=new Set(JSON.parse(localStorage.getItem('collapsed')||'[]'));
function saveCollapsed(){localStorage.setItem('collapsed',JSON.stringify([...collapsed]))}
function hm(t){const d=new Date(t*1000);return pad(d.getHours())+':'+pad(d.getMinutes())}
function when(t){if(!t||t<1700000000)return'—';const d=new Date(t*1000),n=new Date();
 return d.toDateString()==n.toDateString()?hm(t):pad(d.getDate())+'.'+pad(d.getMonth()+1)+' '+hm(t)}
function ago(s){s=Math.max(0,Math.round(s));if(s<60)return s+' s';if(s<3600)return Math.floor(s/60)+' min';if(s<86400)return Math.floor(s/3600)+' h '+Math.floor(s%3600/60)+' min';return Math.floor(s/86400)+' d'}
function muteText(u){return u>=FOREVER?'do odwołania':'do '+when(u)}
async function api(path,opt){const r=await fetch(path,opt);const t=await r.text();if(!r.ok)throw new Error(t||r.status);return t}
async function act(a,extra=''){try{await api('/api/action?do='+a+extra,{method:'POST'})}catch(e){alert(e.message)}
 setTimeout(async()=>{await poll();if(sheetTarget)refreshTargetSheet()},300)}
function flash(el,txt){const o=el.textContent;el.textContent=txt;setTimeout(()=>el.textContent=o,1500)}

/* ---------------- tabs ---------------- */
function show(v){view=v;document.querySelectorAll('.view').forEach(s=>s.classList.toggle('on',s.dataset.view==v));
 document.querySelectorAll('#tabs button').forEach(b=>b.classList.toggle('on',b.dataset.v==v));
 if(v=='dziennik')loadLog();if(v=='serwer')api('/api/ipmi?op=status',{method:'POST'}).then(poll,poll);if(v=='ustawienia'&&!$('tj').value)loadJson();window.scrollTo({top:0});history.replaceState(null,'','#'+v)}
$('tabs').onclick=e=>{const b=e.target.closest('button');if(b)show(b.dataset.v)};

/* ---------------- status polling ---------------- */
async function poll(){
 try{S=JSON.parse(await api('/api/status'));lastOk=Date.now();render()}catch(e){}
 document.body.classList.toggle('stale',Date.now()-lastOk>12000);
 if(Date.now()-lastOk>12000){$('state').className='state bad';$('state').innerHTML='<span class="headline">Brak połączenia ze strażnikiem</span><span class="sub">ostatnie dane '+ago((Date.now()-lastOk)/1000)+' temu</span>';$('selfLed').className='led down'}
}
function kidsMap(ts){const k={};ts.forEach(t=>{if(t.parent)(k[t.parent]=k[t.parent]||[]).push(t)});return k}
function cls(t){if(!t.enabled||t.type=='separator')return'';if(t.state=='down')return t.suppressed?'sup':'down';if(t.fails)return'warn';if(t.state=='up')return'up';return''}

function render(){
 const d=S,ts=d.targets.filter(t=>t.type!='separator');
 $('clock').textContent=hm(d.now);$('selfLed').className='led ident';
 // LED strip: one diode per post, in patrol order
 $('strip').innerHTML=ts.map(t=>`<span class="led ${cls(t)}${d.probing==t.id?' scan':''}" title="${esc(t.name)}" onclick="goto('${esc(t.id)}')"></span>`).join('');
 // state line + fault banner
 const roots=ts.filter(t=>t.enabled&&t.state=='down'&&!t.suppressed);const warn=ts.filter(t=>t.enabled&&t.state!='down'&&t.fails);
 const kids=kidsMap(d.targets);
 const sub=`obchód ${d.last_sweep?ago(d.now-d.last_sweep)+' temu':'—'} · następny za ${d.last_sweep?ago(Math.max(0,d.last_sweep+d.interval-d.now)):'—'}`;
 let st;
 if(roots.length){st=`<span class="headline">${roots.length==1?esc(roots[0].name)+' nie odpowiada':roots.length+' posterunki nie odpowiadają'}</span>`}
 else if(warn.length){st=`<span class="headline">${warn.length==1?esc(warn[0].name)+' gubi odpowiedzi':warn.length+' posterunki gubią odpowiedzi'}</span>`}
 else st=`<span class="headline">Wszystko działa</span>`;
 $('state').className='state '+(roots.length?'bad':warn.length?'warn':'ok');$('state').innerHTML=st+`<span class="sub mono">${d.up}/${d.total} · ${sub}</span>`;
 const chips=[];
 if(d.muted_until)chips.push(`<span class="chip act">🔕 wyciszone ${muteText(d.muted_until)}</span>`);
 if(!d.tg.configured)chips.push('<span class="chip fault">Telegram nieskonfigurowany</span>');else if(d.tg.error)chips.push(`<span class="chip fault" title="${esc(d.tg.error)}">Telegram: błąd wysyłki</span>`);else if(d.tg.queue)chips.push(`<span class="chip act">Telegram: w kolejce ${d.tg.queue}</span>`);
 if(d.mqtt.host&&d.mqtt.status!='połączone')chips.push(`<span class="chip act">MQTT: ${esc(d.mqtt.status)}</span>`);
 if(d.rescue.simulating)chips.push('<span class="chip ident">🧪 symulacja awarii serwera</span>');
 if(d.rssi<-75)chips.push(`<span class="chip act">słabe Wi-Fi ${d.rssi} dBm</span>`);
 $('chips').innerHTML=chips.join('');
 $('alarm').innerHTML=roots.length?`<div class="alarm">${roots.map(t=>{const n=cntKids(t.id,kids);return`<div class="item"><span class="led down"></span><div style="flex:1"><b>${esc(t.name)}</b><div class="silk" style="text-transform:none;letter-spacing:0">od ${when(t.since)} · ${esc(t.err)}${n?` · zależnych: ${n}`:''}</div></div><button class="ghost" onclick="openTarget('${esc(t.id)}')">Szczegóły</button></div>`}).join('')}
   <div class="actions"><button onclick="act('mute','&min=60')">Wycisz wszystko na 1 h</button><button onclick="act('sweep')">Sprawdź teraz</button>${roots.some(t=>t.id==d.rescue.target)?'<button class="danger" onclick="show(\'serwer\')">Restart serwera…</button>':''}</div></div>`:'';
 renderTree(d.targets,kids);renderServer(d);fillForms(d);
 $('srvDot').style.background=roots.some(t=>t.id==d.rescue.target)?'var(--fault)':d.rescue.simulating?'var(--ident)':'transparent';
}
function cntKids(id,kids){let n=0;(kids[id]||[]).forEach(c=>{n++;n+=cntKids(c.id,kids)});return n}
function sub(id,kids){let n=0,bad=0;(kids[id]||[]).forEach(c=>{n++;if(c.enabled&&c.state=='down')bad++;const s=sub(c.id,kids);n+=s.n;bad+=s.bad});return{n,bad}}

/* ---------------- tree ---------------- */
function valOf(t){
 if(!t.enabled)return['wył.',''];
 if(t.state=='down')return['od '+when(t.since),t.suppressed?'':'bad'];
 if(t.fails)return[t.fails+'× brak','warn'];
 if(t.state=='up')return[t.ms+' ms',''];return['…',''];
}
function desc(t){let s=t.type=='http'&&t.port==443?'https '+t.host:t.type+' '+t.host;if(t.port&&!(t.type=='http'&&t.port==443))s+=':'+t.port;if(t.type=='http'&&t.path!='/')s+=t.path;if(t.type=='dns')s+=' → '+t.query;
 if(t.every>1)s+=' · co '+ago(t.every*(S?S.interval:30));if(t.cert_days!=null)s+=' · cert '+t.cert_days+' d';return s}
function renderTree(all,kids){
 let html='',section=null;
 const row=(t,depth,last,prefix)=>{
  const ch=kids[t.id]||[],open=!collapsed.has(t.id),[v,vc]=valOf(t);
  const guide=depth?prefix+(last?'└ ':'├ '):'';
  const tw=ch.length?`<span class="tw" onclick="event.stopPropagation();toggle('${esc(t.id)}')">${open?'▾':'▸'}</span>`:'<span class="tw"></span>';
  let badge='';if(ch.length&&!open){const s=sub(t.id,kids);badge=`<span class="badge${s.bad?' bad':''}">+${s.n}${s.bad?' · '+s.bad+' leży':''}</span>`}
  const muted=t.muted_until?'<span class="muted-ico" title="wyciszony">🔕</span>':'';
  let h=`<div class="row${t.enabled?'':' off'}" id="r-${esc(t.id)}" onclick="openTarget('${esc(t.id)}')"><span class="ind">${guide}${tw}</span><span class="led ${cls(t)}"></span><span class="nm"><b>${esc(t.name)}${muted}${badge}</b><small>${esc(desc(t))}</small></span><span class="val ${vc}">${v}</span></div>`;
  if(open)ch.forEach((c,i)=>h+=row(c,depth+1,i==ch.length-1,depth?prefix+(last?'  ':'│ '):''));
  return h};
 const secHidden=()=>section&&collapsed.has(section);
 all.forEach(t=>{
  if(t.type=='separator'){section=t.id;html+=`<div class="sec" onclick="toggle('${esc(t.id)}')"><span class="tw">${collapsed.has(t.id)?'▸':'▾'}</span><span class="silk">${esc(t.name)}</span></div>`;return}
  if(!t.parent&&!secHidden())html+=row(t,0,true,'')});
 $('tree').innerHTML=html||'<div class="empty">Brak celów — dodaj pierwszy przyciskiem „+ Dodaj”.</div>';
}
function toggle(id){collapsed.has(id)?collapsed.delete(id):collapsed.add(id);saveCollapsed();render()}
function setAll(c){collapsed.clear();if(c&&S)S.targets.forEach(t=>collapsed.add(t.id));saveCollapsed();render()}
function goto(id){show('cele');const t=S.targets.find(x=>x.id==id);let p=t&&t.parent;while(p){collapsed.delete(p);p=(S.targets.find(x=>x.id==p)||{}).parent}saveCollapsed();render();
 const el=$('r-'+id);if(el){el.scrollIntoView({block:'center',behavior:'smooth'});el.classList.remove('flash');void el.offsetWidth;el.classList.add('flash')}}
function muteAll(sel){const v=sel.value;sel.value='';if(v==='')return;v=='0'?act('unmute'):act('mute','&min='+v)}

/* ---------------- target card ---------------- */
async function openTarget(id){sheetTarget=id;openSheet('<div class="empty">Wczytuję…</div>');await refreshTargetSheet(true)}
async function refreshTargetSheet(first){
 const id=sheetTarget,t=S.targets.find(x=>x.id==id);if(!t){closeSheet();return}
 let h=null;try{h=JSON.parse(await api('/api/target?id='+encodeURIComponent(id)))}catch(e){}
 if(sheetTarget!=id)return;
 const hist=h?h.hist:[],ok=hist.filter(x=>x!==null),lost=hist.length-ok.length;
 const avg=ok.length?Math.round(ok.reduce((a,b)=>a+b,0)/ok.length):null,max=ok.length?Math.max(...ok):null;
 const [v,vc]=valOf(t);
 const muteBtns=t.muted_until?`<button onclick="act('unmute','&id=${esc(id)}')">Odcisz (${muteText(t.muted_until)})</button>`:`<select onchange="if(this.value)act('mute','&id=${esc(id)}&min='+this.value)"><option value="">Wycisz…</option><option value="60">na 1 h</option><option value="240">na 4 h</option><option value="1440">na 24 h</option><option value="-1">do odwołania</option></select>`;
 $('sheetBody').innerHTML=`
  <div style="display:flex;gap:10px;align-items:center"><span class="led ${cls(t)}" style="width:12px;height:12px"></span><b style="font-size:17px;flex:1">${esc(t.name)}</b><span class="val mono ${vc}">${v}</span></div>
  <div class="silk mono" style="text-transform:none;letter-spacing:0;margin:4px 0 12px">${esc(desc(t))}${t.parent?' · za: '+esc(t.parent):''}</div>
  ${t.err?`<div class="err-msg" style="margin-bottom:10px">${esc(t.err)}</div>`:''}
  ${sparkSvg(hist)}
  <div class="stats"><div><b>${avg??'—'}${avg!==null?' ms':''}</b><span>średnio</span></div><div><b>${max??'—'}${max!==null?' ms':''}</b><span>najwolniej</span></div><div><b>${hist.length?Math.round(100*lost/hist.length):0}%</b><span>bez odpowiedzi</span></div></div>
  ${t.full_ms?`<p class="note">Połączenie z serwerem: <b class="mono" style="color:var(--label)">${t.ms} ms</b> · całe zapytanie https: <span class="mono">${t.full_ms} ms</span>, z czego ~${Math.max(0,t.full_ms-t.ms)} ms to szyfrowanie liczone przez samego strażnika (mikrokontroler), nie czas serwera.</p>`:''}
  ${t.cert_days!=null?`<p class="note" style="color:${t.cert_days<14?'var(--fault)':t.cert_days<30?'var(--act)':'var(--legend)'}">Certyfikat wygasa za ${t.cert_days} dni${t.cert_days<30?' — Let\'s Encrypt odnawia 30 dni przed końcem, więc coś jest nie tak':''}</p>`:''}
  ${t.needs?`<p class="note">Wymaga też: ${esc(t.needs)} — gdy któreś leży, ten cel nie wysyła osobnego alarmu.</p>`:''}
  <p class="note">ostatnie ${hist.length} sprawdzeń (${ago(hist.length*(S.interval||30))}) · stan od ${when(t.since)}</p>
  <div class="acts">
   <button class="pri" onclick="act('sweep')">Sprawdź teraz</button>${muteBtns}
   ${t.enabled?`<button onclick="if(confirm('Przestać sprawdzać ${esc(t.name).replace(/'/g,'')}? Zostanie na liście.'))act('disable','&id=${esc(id)}')">Wyłącz sprawdzanie</button>`:`<button onclick="act('enable','&id=${esc(id)}')">Włącz sprawdzanie</button>`}
   <button onclick="openForm('${esc(id)}')">Edytuj</button>
   <button class="ghost" style="grid-column:1/-1" onclick="closeSheet()">Zamknij</button>
  </div>`;
}
function sparkSvg(h){
 if(!h.length)return'<div class="spark" style="display:grid;place-items:center;color:var(--silk);font-size:12px">brak historii — pojawi się po kilku obchodach</div>';
 const W=300,H=60,ok=h.filter(x=>x!==null),mx=Math.max(10,...ok),st=W/Math.max(1,h.length-1);let pts=[],fails='';
 h.forEach((v,i)=>{const x=(h.length==1?W:i*st).toFixed(1);if(v===null){fails+=`<line x1="${x}" y1="${H}" x2="${x}" y2="${H-14}" stroke="#ff5a52" stroke-width="2"/>`;pts.push(null)}else pts.push(x+','+(H-4-(v/mx)*(H-12)).toFixed(1))});
 let path='',seg=[];pts.forEach(p=>{if(p)seg.push(p);else{if(seg.length)path+=`<polyline points="${seg.join(' ')}"/>`;seg=[]}});if(seg.length)path+=`<polyline points="${seg.join(' ')}"/>`;
 return`<svg class="spark" viewBox="0 0 ${W} ${H}" preserveAspectRatio="none"><g fill="none" stroke="#3ddc84" stroke-width="1.6" vector-effect="non-scaling-stroke">${path}</g>${fails}<text x="4" y="11" fill="#6c7784" font-size="9" font-family="monospace">${mx} ms</text></svg>`}

/* ---------------- add / edit form ---------------- */
let F=null;
async function openForm(editId){
 let list;try{list=JSON.parse(await api('/api/targets'))}catch(e){alert('Nie wczytam listy: '+e.message);return}
 const cur=editId?list.find(x=>x.id==editId):{type:'ping',name:'',id:'',host:'',parent:''};
 F={list,editId,t:JSON.parse(JSON.stringify(cur))};renderForm();openSheet(null)}
function descendants(list,id){const out=new Set([id]);let ch=true;while(ch){ch=false;list.forEach(x=>{if(x.parent&&out.has(x.parent)&&!out.has(x.id)){out.add(x.id);ch=true}})}return out}
function renderForm(){
 const t=F.t,isSep=t.type=='separator',bad=F.editId?descendants(F.list,F.editId):new Set();
 const parents=F.list.filter(x=>x.type!='separator'&&!bad.has(x.id));
 const typeBtn=k=>`<button type="button" class="${t.type==k?'on':''}" onclick="syncForm();F.t.type='${k}';renderForm()">${k}</button>`;
 $('sheetBody').innerHTML=`<b style="font-size:17px">${F.editId?'Edytuj cel':'Nowy cel'}</b>
  <form id="tf" style="display:grid;gap:12px;margin-top:12px" onsubmit="return saveForm(event)">
   ${isSep?`<label class="f">Nazwa sekcji<input name="name" value="${esc(t.name)}" required></label>`:`
   <div class="types">${['ping','tcp','http','dns'].map(typeBtn).join('')}</div>
   <p class="note" style="margin:0">${{ping:'Czy maszyna w ogóle żyje w sieci.',tcp:'Czy usługa słucha na porcie (np. baza, SSH, Proxmox).',http:'Czy strona/aplikacja odpowiada właściwym kodem.',dns:'Czy serwer DNS naprawdę rozwiązuje nazwy.'}[t.type]}</p>
   <label class="f">Nazwa<input name="name" value="${esc(t.name)}" required placeholder="np. Drukarka Q2" oninput="autoId(this.value)"></label>
   <label class="f">Identyfikator<small>bez spacji; w Telegramie: /wycisz ${esc(t.id||'id')}</small><input name="id" value="${esc(t.id)}" required pattern="[a-z0-9_-]+" ${F.editId?'readonly':''}></label>
   <label class="f">Adres<input name="host" value="${esc(t.host)}" required placeholder="192.168.1.x" inputmode="decimal"></label>
   ${t.type=='tcp'||t.type=='http'?`<label class="f">Port<input name="port" type="number" value="${esc(t.port??(t.type=='http'?80:''))}" ${t.type=='tcp'?'required':''} inputmode="numeric"></label>`:''}
   ${t.type=='http'?`<label class="f">Ścieżka<input name="path" value="${esc(t.path??'/')}"></label>
     <label class="f">Wymagany kod<small>0 = każda odpowiedź poniżej 500</small><input name="expect" type="number" value="${esc(t.expect??0)}" inputmode="numeric"></label>
     <label class="f">Szyfrowanie<select name="tls"><option value="0">http</option><option value="1" ${t.tls?'selected':''}>https (bez sprawdzania certyfikatu)</option></select></label>`:''}
   ${t.type=='dns'?`<label class="f">Nazwa do rozwiązania<input name="query" value="${esc(t.query??'google.com')}"></label>`:''}
   <label class="f">Zależy od<small>gdy rodzic leży, ten cel nie wysyła osobnego alarmu</small><select name="parent"><option value="">— nic (poziom główny)</option>${parents.map(p=>`<option value="${esc(p.id)}" ${t.parent==p.id?'selected':''}>${esc(p.name)}</option>`).join('')}</select></label>`}
   <div class="acts" style="margin-top:4px"><button class="pri">Zapisz</button>
    ${F.editId?`<button type="button" class="danger" onclick="delTarget()">Usuń</button>`:`<button type="button" onclick="syncForm();F.t={type:'separator',name:F.t.name||''};renderForm()">To ma być sekcja</button>`}
    <button type="button" class="ghost" style="grid-column:1/-1" onclick="closeSheet()">Anuluj</button></div>
   <span class="err-msg" id="tfmsg"></span>
  </form>`}
// keep what was typed when the form is re-rendered (type switch)
function syncForm(){const f=$('tf');if(!f)return;new FormData(f).forEach((v,k)=>{F.t[k]=k=='port'||k=='expect'?(v===''?undefined:+v):k=='tls'?v=='1':v})}
function autoId(n){if(F.editId)return;const i=document.querySelector('#tf [name=id]');if(i&&!i.dataset.touched)i.value=n.toLowerCase().normalize('NFD').replace(/[̀-ͯ]/g,'').replace(/ł/g,'l').replace(/[^a-z0-9]+/g,'-').replace(/^-|-$/g,'').slice(0,24)}
document.addEventListener('input',e=>{if(e.target.name=='id')e.target.dataset.touched=1});
function fixOrder(list){for(let k=0;k<list.length*2;k++){let moved=false;for(let i=0;i<list.length;i++){const p=list[i].parent;if(!p)continue;const pi=list.findIndex(x=>x.id==p);if(pi>i){const [x]=list.splice(i,1);list.splice(list.findIndex(y=>y.id==p)+1,0,x);moved=true;break}}if(!moved)break}return list}
async function saveForm(e){e.preventDefault();const fd=new FormData($('tf')),t=F.t;
 const orig=F.editId?F.list.find(x=>x.id==F.editId)||{}:{};   // keep fields the form does not show (every, needs)
 const o=t.type=='separator'?{type:'separator',name:fd.get('name'),id:t.id||'sek-'+Date.now().toString(36)}:{id:fd.get('id').trim(),name:fd.get('name').trim(),type:t.type,host:fd.get('host').trim()};
 if(fd.get('port'))o.port=+fd.get('port');
 if(t.type=='http'){o.path=fd.get('path')||'/';o.expect=+fd.get('expect')||0;if(fd.get('tls')=='1')o.tls=true}
 if(t.type=='dns')o.query=fd.get('query');
 if(fd.get('parent'))o.parent=fd.get('parent');
 if(t.enabled===false)o.enabled=false;
 if(orig.every)o.every=orig.every;if(orig.needs)o.needs=orig.needs;
 let list=F.list.slice();
 if(F.editId){list[list.findIndex(x=>x.id==F.editId)]=o}
 else{if(o.id&&list.some(x=>x.id==o.id)){$('tfmsg').textContent='Taki identyfikator już jest.';return false}
  if(o.parent){const d=descendants(list,o.parent);let last=-1;list.forEach((x,i)=>{if(d.has(x.id))last=i});list.splice(last+1,0,o)}else list.push(o)}
 fixOrder(list);
 try{await api('/api/targets',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(list)});closeSheet();$('tj').value='';poll()}
 catch(err){$('tfmsg').textContent=err.message}return false}
async function delTarget(){const id=F.editId;if(F.list.some(x=>x.parent==id)){$('tfmsg').textContent='Najpierw zmień rodzica celów, które od niego zależą.';return}
 if(!confirm('Usunąć ten cel z listy?'))return;
 try{await api('/api/targets',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(F.list.filter(x=>x.id!=id))});closeSheet();$('tj').value='';poll()}catch(err){$('tfmsg').textContent=err.message}}

/* ---------------- sheet ---------------- */
function openSheet(html){if(html!==null)$('sheetBody').innerHTML=html;$('scrim').classList.add('on');$('sheet').classList.add('on')}
function closeSheet(){$('scrim').classList.remove('on');$('sheet').classList.remove('on');sheetTarget=null;F=null}
document.addEventListener('keydown',e=>{if(e.key=='Escape')closeSheet()});

/* ---------------- server ---------------- */
function renderServer(d){const r=d.rescue,t=d.targets.find(x=>x.id==r.target);
 $('pwName').textContent=t?t.name:r.target||'—';
 const on=r.power=='włączone',off=r.power=='WYŁĄCZONE';
 $('pwLed').className='led '+(!r.power_checked?'':on?(t&&t.state=='down'?'down':'ident'):off?'':'warn');
 $('pwText').textContent='zasilanie: '+(r.power_checked?r.power:'nie sprawdzano')+(t?' · system: '+(t.state=='up'?'odpowiada':t.state=='down'?'NIE odpowiada':'—'):'');
 $('srvMode').innerHTML=r.dry_run?'<span class="chip ident">🧪 testowy</span>':'<span class="chip fault">⚠️ prawdziwy</span>';
 $('srvAuto').innerHTML=r.auto?`<span class="chip ${r.in_window?'act':''}">włączony ${r.win_start}–${r.win_end}${r.in_window?' · teraz aktywny':''}</span>`:'<span class="chip">wyłączony — tylko pytam</span>';
 $('srvState').textContent=r.state+(r.simulating?' (symulacja)':'');
 $('srvBmc').textContent=(r.ipmi_user||'—')+'@'+(r.ipmi_host||'—');
 $('simBox').style.display=r.dry_run?'':'none';
 $('holdNote').textContent=r.dry_run?'Tryb testowy: przytrzymanie przejdzie całą procedurę, ale do BMC nic nie pójdzie.':'Działa jak przycisk RESET na obudowie: na chwilę padnie wszystko, co działa na serwerze.';
}
async function ipmi(op){$('ipmiMsg').textContent='⏳ pytam BMC…';try{$('ipmiMsg').textContent=await api('/api/ipmi?op='+op,{method:'POST'})}catch(e){$('ipmiMsg').textContent='❌ '+e.message}poll()}
(function(){const b=$('holdReset');let t0=0,raf=0;const HOLD=2000;
 const stop=()=>{cancelAnimationFrame(raf);t0=0;b.style.setProperty('--p',0)};
 const tick=()=>{const p=Math.min(1,(performance.now()-t0)/HOLD);b.style.setProperty('--p',p);if(p<1)raf=requestAnimationFrame(tick);else{stop();ipmi('reset')}};
 b.addEventListener('pointerdown',e=>{e.preventDefault();t0=performance.now();raf=requestAnimationFrame(tick)});
 ['pointerup','pointerleave','pointercancel'].forEach(ev=>b.addEventListener(ev,stop));
 b.addEventListener('contextmenu',e=>e.preventDefault())})();

/* ---------------- log ---------------- */
$('logSeg').onclick=e=>{const b=e.target.closest('button');if(!b)return;logFilter=b.dataset.f;document.querySelectorAll('#logSeg button').forEach(x=>x.classList.toggle('on',x==b));loadLog()};
const FAULT=/^(DOWN|UP|Awaria|IPMI|TEST —|Utracono|Wi-Fi wróciło|Restart )/;
async function loadLog(){
 let ev=[],tg=[];try{[ev,tg]=await Promise.all([api('/api/log').then(JSON.parse),api('/api/tglog').then(JSON.parse)])}catch(e){$('log').innerHTML='<div class="empty">Nie udało się wczytać dziennika.</div>';return}
 let items=[];
 if(logFilter!='tg')ev.forEach(x=>{const f=FAULT.test(x.m);if(logFilter=='all'||(logFilter=='fault'&&f)||(logFilter=='sys'&&!f))items.push({t:x.t,html:`<div class="msg">${esc(x.m)}</div>`})});
 if(logFilter=='all'||logFilter=='tg'){const L={sent:['↗ wysłano','var(--link)'],retry:['↻ nie doszło, ponawiam','var(--act)'],dropped:['✕ odrzucona','var(--fault)'],in:['↘ od Ciebie','var(--ident)']};
  tg.forEach(x=>{const [l,c]=L[x.st]||['?',''];items.push({t:x.t,html:`<div class="tg"><span class="led" style="width:7px;height:7px;background:${c}"></span>Telegram · ${l}${x.err?' · '+esc(x.err):''}</div><div class="msg">${esc(x.text)}</div>`})})}
 items.sort((a,b)=>b.t-a.t);
 let html='',day='';items.forEach(x=>{const d=x.t>1700000000?new Date(x.t*1000).toLocaleDateString('pl-PL',{weekday:'long',day:'numeric',month:'long'}):'przed ustawieniem zegara';if(d!=day){day=d;html+=`<div class="day silk">${d}</div>`}
  html+=`<div class="le"><time>${x.t>1700000000?hm(x.t):'—'}</time><div>${x.html}</div></div>`});
 $('log').innerHTML=html||'<div class="empty">Pusto.</div>'}

/* ---------------- settings ---------------- */
function fillForms(d){
 $('tgSum').textContent=d.tg.configured?'podłączony · czat '+d.tg.chat:'nieskonfigurowany';
 $('rsSum').textContent=(d.rescue.dry_run?'tryb testowy':'tryb PRAWDZIWY')+' · automat '+(d.rescue.auto?d.rescue.win_start+'–'+d.rescue.win_end:'wyłączony');
 $('mqSum').textContent=d.mqtt.host?d.mqtt.host+' · '+d.mqtt.status:'wyłączone';
 $('wfSum').textContent=`${d.ssid} · ${d.rssi} dBm · ${d.ip}`;
 $('sysSum').textContent='firmware '+d.fw+' · działa '+ago(d.uptime);
 $('sysKv').innerHTML=[['Firmware',d.fw],['Działa od',ago(d.uptime)],['Obchodów',d.sweeps],['Wolna pamięć',Math.round(d.heap/1024)+' kB'],['PSRAM',Math.round(d.psram/1024)+' kB'],['IP',d.ip],['MAC',d.mac],['Wi-Fi',d.rssi+' dBm']].map(([k,v])=>`<dt>${k}</dt><dd class="mono">${esc(v)}</dd>`).join('');
 $('pending').innerHTML=d.tg.pending_chat&&!d.tg.chat_set?`<div class="alarm" style="margin:0 0 12px;border-color:rgba(242,169,59,.5);background:#1b160c">Do bota napisał czat <b>${esc(d.tg.pending_chat)}</b> (${esc(d.tg.pending_name)}). <div class="actions"><button class="pri" onclick="adopt()">To mój czat — użyj</button></div></div>`:'';
 if(formsFilled)return;formsFilled=true;
 const v={interval:d.interval,faillimit:d.fail_limit,remind:d.remind_min,hb:d.heartbeat_hour,tgchat:d.tg.chat,rdry:d.rescue.dry_run?'1':'0',rauto:d.rescue.auto?'1':'0',rwins:d.rescue.win_start,rwine:d.rescue.win_end,rautomin:d.rescue.auto_min,rhung:d.rescue.hung_min,rtgt:d.rescue.target,iphost:d.rescue.ipmi_host,ipuser:d.rescue.ipmi_user,mqhost:d.mqtt.host,mqport:d.mqtt.port,mquser:d.mqtt.user,ssid:d.ssid_cfg};
 document.querySelectorAll('form[data-save] [name]').forEach(i=>{if(i.name in v)i.value=v[i.name]});
 document.querySelector('[name=tgtoken]').placeholder=d.tg.token_set?'ustawiony — wpisz nowy, by zmienić':'123456:ABC…'}
document.querySelectorAll('form[data-save]').forEach(f=>f.onsubmit=async e=>{e.preventDefault();const m=f.querySelector('.msg');
 const fd=new URLSearchParams();new FormData(f).forEach((val,k)=>{if(f.querySelector(`[name="${k}"]`).type=='password'&&!val)return;fd.append(k,val)});
 if(fd.has('rdry')&&fd.get('rdry')=='0'&&S&&S.rescue.dry_run&&!confirm('Przełączyć restart serwera w tryb PRAWDZIWY? Od teraz reset naprawdę pójdzie do BMC.'))return;
 try{const t=await api('/api/settings',{method:'POST',body:fd});m.className='msg ok-msg';m.textContent=t;f.querySelectorAll('input[type=password]').forEach(i=>i.value='');formsFilled=false;poll()}
 catch(err){m.className='msg err-msg';m.textContent=err.message}});
async function adopt(){try{await api('/api/adopt-chat',{method:'POST'})}catch(e){alert(e.message)}formsFilled=false;poll()}
async function loadJson(){try{$('tj').value=await api('/api/targets')}catch(e){}}
async function saveJson(){const m=$('tjmsg');try{m.className='msg ok-msg';m.textContent=await api('/api/targets',{method:'POST',headers:{'Content-Type':'application/json'},body:$('tj').value});poll()}catch(e){m.className='msg err-msg';m.textContent=e.message}}

/* ---------------- start ---------------- */
let timer=null;function loop(){clearTimeout(timer);if(document.visibilityState=='visible'){poll();if(view=='dziennik')loadLog()}timer=setTimeout(loop,view=='dziennik'?6000:3000)}
document.addEventListener('visibilitychange',()=>{if(document.visibilityState=='visible')loop()});
const h=location.hash.slice(1);if(['cele','serwer','dziennik','ustawienia'].includes(h))show(h);
loop();
</script></body></html>)HTML";
