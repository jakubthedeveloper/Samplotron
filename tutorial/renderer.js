/* Deterministic SVG scenes for the interactive build guide. */
window.GuideRenderer = (() => {
'use strict';
const project = window.SAMPLOTRON_GUIDE;
const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'}[c]));
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));
function words(text,max){const lines=[];let line='';for(const word of text.split(/\s+/)){if((line+' '+word).trim().length>max&&line){lines.push(line);line='';}line+=(line?' ':'')+word;}if(line)lines.push(line);return lines;}
function tx(text,x,y,size=30,color='#f0ece0',max=70){return `<text x="${x}" y="${y}" fill="${color}" font-family="Arial, sans-serif" font-size="${size}">${words(text,max).map((s,i)=>`<tspan x="${x}" dy="${i?size*1.35:0}">${esc(s)}</tspan>`).join('')}</text>`;}
function board(id,x,y,w=450,h=280){const a=project.assets.find(a=>a.id===id);return `<image href="${esc(a.image)}" x="${x}" y="${y}" width="${w}" height="${h}" preserveAspectRatio="xMidYMid meet"/>`+tx(a.name,x+w/2,y+h+30,24,'#c7c3b7',38).replace('<text ','<text text-anchor="middle" ');}
// Captured firmware pixels are embedded as SVG, without browser font substitution.
function oledMarkup(name,x,y,w,h){
 const screen=window.OLED_SCREENS[name];if(!screen)return '';
 return screen.replace('<svg ',`<svg x="${x}" y="${y}" `).replace('width="128"','width="'+w+'"').replace('height="64"','height="'+h+'"');
}

function welcomeScene(s,t){
 const intro=s.direction.introduction;
 let art=centered('WELCOME TO THE INTERACTIVE BUILD GUIDE',960,290,24,65,YELLOW);
 art+=centered(s.caption,960,395,52,55);
 art+=centered(intro.description,960,552,33,82);
 art+=`<a href="${esc(intro.link)}" target="_blank" rel="noopener noreferrer" aria-label="View the Samplotron repository"><rect x="660" y="701" width="600" height="76" rx="10" fill="#211e11" stroke="${YELLOW}" stroke-width="2"/>${centered('View the Samplotron repository ↗',960,750,29,50,YELLOW)}</a>`;
 art+=centered('Choose a step below, or press Play to watch the demonstration.',960,835,25,90,MUTED);
 return {art,footer:noteFooter('Build at your own pace. Pause, replay, or skip to any step.'),badge:'START HERE',credit:''};
}
// Time-driven SVG direction. Every frame can be sought independently.
const YELLOW='#ffd42a', WHITE='#eee8d7', MUTED='#aaa18b';
const PAD_LABELS=['1','2','3','A','4','5','6','B','7','8','9','C','*','0','#','D'];
const ease=x=>{x=clamp(x,0,1);return x*x*(3-2*x);};
function centered(text,x,y,size=36,max=65,color=WHITE){return tx(text,x,y,size,color,max).replace('<text ','<text text-anchor="middle" ');}
function phase(items,t,end){let index=0;for(let i=0;i<items.length;i++)if(items[i].at<=t)index=i;return {item:items[index],index,elapsed:Math.max(0,t-items[index].at),length:(items[index+1]?.at??end)-items[index].at};}
function bar(x,y,w,progress,color=YELLOW){return `<rect x="${x}" y="${y}" width="${w}" height="3" fill="#494437"/><rect x="${x}" y="${y}" width="${w*clamp(progress,0,1)}" height="3" fill="${color}"/>`;}
function noteFooter(text,prominent=false){
 const size=prominent?60:32,max=prominent?52:92;
 const y=prominent?(words(text,max).length>1?952:984):966;
 return `<rect y="895" width="1920" height="185" fill="#080808"/><g font-weight="${prominent?600:400}">${centered(text,960,y,size,max,prominent?YELLOW:WHITE)}</g>`;
}
function steel(){return `<defs>
 <radialGradient id="steelLight" cx="32%" cy="10%" r="95%"><stop stop-color="#242423"/><stop offset=".65" stop-color="#111110"/><stop offset="1" stop-color="#070707"/></radialGradient>
 <linearGradient id="panel" x2="0" y2="1"><stop stop-color="#30302b"/><stop offset=".14" stop-color="#171716"/><stop offset="1" stop-color="#0a0a09"/></linearGradient>
 <radialGradient id="knob"><stop stop-color="#3e3e37"/><stop offset=".78" stop-color="#1b1b18"/><stop offset="1" stop-color="#080808"/></radialGradient>
 <linearGradient id="keyFace" x2="0" y2="1"><stop stop-color="#393931"/><stop offset="1" stop-color="#171714"/></linearGradient>
 <filter id="powder" x="0" y="0" width="100%" height="100%"><feTurbulence type="fractalNoise" baseFrequency=".72" numOctaves="3" seed="37" stitchTiles="stitch"/><feColorMatrix type="saturate" values="0"/><feComponentTransfer><feFuncA type="linear" slope=".075"/></feComponentTransfer></filter>
 </defs><rect width="1920" height="1080" fill="url(#steelLight)"/><rect width="1920" height="1080" filter="url(#powder)" opacity=".75"/>`;}
function pressEnvelope(t,hold=false){const length=hold?1.2:.64;if(t<0||t>length)return 0;return ease(t/.16)*(1-ease((t-(length-.18))/.18));}
function touch(x,y,t,hold=false){const q=pressEnvelope(t,hold);if(!q)return '';return `<g opacity="${q}"><circle cx="${x+24*(1-q)}" cy="${y-32*(1-q)}" r="23" fill="#ffe883" fill-opacity=".15" stroke="#ffe883" stroke-width="3"/><circle cx="${x}" cy="${y}" r="7" fill="#fff5ba"/></g>`;}
function knob(x,y,r,label,press=0,angle=0){let art=`<ellipse cx="${x}" cy="${y+12}" rx="${r+5}" ry="${r}" fill="#020202"/><circle cx="${x}" cy="${y+press*6}" r="${r}" fill="url(#knob)" stroke="#5f5b48" stroke-width="2"/>`;
 for(let i=0;i<28;i++){const a=(i*360/28+angle)*Math.PI/180;art+=`<path d="M${x+Math.cos(a)*(r-5)} ${y+press*6+Math.sin(a)*(r-5)}L${x+Math.cos(a)*(r-13)} ${y+press*6+Math.sin(a)*(r-13)}" stroke="#535044" stroke-width="2"/>`;}
 art+=`<g transform="translate(${x} ${y+press*6}) rotate(${angle})"><path d="M0 ${-r+19}v18" stroke="${YELLOW}" stroke-width="7" stroke-linecap="round"/></g>`;
 if(press)art+=`<circle cx="${x}" cy="${y}" r="${r+8}" fill="none" stroke="${YELLOW}" opacity="${press}" stroke-width="3"/>`;
 return art+centered(label,x,y+r+40,20,25,MUTED);
}
function keypad(x,y,size,active=-1,press=0,labels=null){let art='';const pitch=size/4,w=pitch-12;
 for(let i=0;i<16;i++){const px=x+(i%4)*pitch,py=y+Math.floor(i/4)*pitch,q=i===active?press:0;
  art+=`<rect x="${px}" y="${py+9}" width="${w}" height="${w}" rx="10" fill="#030303"/><rect x="${px}" y="${py+q*7}" width="${w}" height="${w}" rx="10" fill="${q>.1?YELLOW:'url(#keyFace)'}" stroke="${i===active?'#e2be36':'#504c3e'}" stroke-width="${i===active?2.5:1.5}"/>`;
  art+=centered(labels?labels[i]:String(i+1).padStart(2,'0'),px+w/2,py+q*7+w/2+7,20,6,q>.1?'#191500':'#bcb39b');
 }return art;
}
function readFirst(s,t){const d=s.direction,seconds=d.readSeconds,lines=words(s.caption,56),offset=(1-ease(t/.5))*10;
 let art=centered('READ THIS FIRST',960,300,22,40,YELLOW);
 art+=`<g transform="translate(0 ${offset})" opacity="${.6+.4*ease(t/.45)}">${centered(s.caption,960,475-(lines.length-1)*31,51,56)}</g>`;
 art+=bar(790,745,340,t/seconds);
 art+=centered(s.wires.length?'NEXT / CONNECT ONE WIRE AT A TIME':d.mode==='device'?'NEXT / SEE IT IN ACTION':'NEXT / FOLLOW THE CONTROLS',960,805,20,75,MUTED);
 return {art,footer:'',badge:'READ'};
}
function icon(name,x,y){const common=`fill="none" stroke="${YELLOW}" stroke-width="3" stroke-linejoin="round"`;
 if(name==='folder')return `<path d="M${x-38} ${y-21}h28l10 10h38v44h-76z" ${common}/>`;
 if(name==='sd')return `<path d="M${x-23} ${y-34}h40v68h-47v-57z" ${common}/><path d="M${x-14} ${y-20}v18m12-18v18m12-18v18" ${common}/>`;
 if(name==='terminal')return `<rect x="${x-40}" y="${y-28}" width="80" height="56" rx="6" ${common}/><path d="M${x-27} ${y-10}l12 10-12 10m23 0h22" ${common}/>`;
 if(name==='oled')return `<rect x="${x-44}" y="${y-27}" width="88" height="54" rx="5" ${common}/><path d="M${x-27} ${y-10}h54m-54 13h36m-36 13h47" ${common}/>`;
 if(name==='usb')return `<path d="M${x} ${y+35}v-62m-8 8l8-10 8 10m-8 17l-24-13v-12m24 42l24-13v-12" ${common}/>`;
 return `<path d="M${x-40} ${y}h12l8-22 12 44 12-52 12 43 8-13h16" ${common}/>`;
}
function cardsScene(s,t){const f=phase(s.direction.cards,t,s.duration),c=f.item,e=ease(f.elapsed/.45),headlineLines=words(c.heading,43).length;
 let art=icon(c.icon,960,275)+`<g opacity="${.6+.4*e}" transform="translate(0 ${(1-e)*12})">`;
 art+=centered(c.heading,960,400,56,43);
 if(c.files){
  art+=centered(c.text,960,465,32,80);
  const active=Math.min(c.files.length-1,Math.floor(f.elapsed/f.length*c.files.length));
  c.files.forEach((file,i)=>{const x=335+(i%2)*650,y=507+Math.floor(i/2)*83;art+=`<rect x="${x}" y="${y}" width="600" height="64" rx="9" fill="#0a0a08" stroke="${i===active?YELLOW:'#595443'}" stroke-width="${i===active?3:1}"/>`+centered(file,x+300,y+43,32,35,i===active?YELLOW:WHITE);});
 }else if(c.commandLines){
  const height=c.commandLines.length*36+36;art+=`<rect x="245" y="439" width="1430" height="${height}" rx="12" fill="#050505" stroke="#6b5c28"/>`;
  c.commandLines.forEach((line,i)=>{art+=`<text xml:space="preserve" x="282" y="${479+i*36}" fill="${YELLOW}" font-family="monospace" font-size="27">${esc(line)}</text>`;});
 }else if(c.showCommand||c.command){art+=`<rect x="255" y="465" width="1410" height="115" rx="12" fill="#050505" stroke="#6b5c28"/>`+centered(c.command||s.command,960,536,39,65,YELLOW);
 }else{
  if(c.text)art+=centered(c.text,960,headlineLines>1?570:515,42,61);
  if(c.link)art+=`<a href="${esc(c.link)}" target="_blank" rel="noopener noreferrer">${centered(c.link,960,604,27,85,YELLOW)}</a>`;
 }
 art+=centered(c.detail||'',960,c.commandLines?748:705,27,88,MUTED)+'</g>';
 art+=bar(790,812,340,f.elapsed/f.length)+centered(`${f.index+1} / ${s.direction.cards.length}`,960,862,19,20,MUTED);
 return {art,footer:'',badge:'READ'};
}

function spotlightScene(s,t){const f=phase(s.direction.focusCues,t,s.duration),c=f.item;let art='';
 const cues=s.direction.focusCues;
 cues.forEach((item,i)=>{const x=86+i*352,active=i===f.index,enter=ease(f.elapsed/.6),scale=active?1+.035*enter:1;
  art+=`<g data-focus="${esc(item.asset)}" opacity="${active?1:.34}" transform="translate(${x+160} 408) scale(${scale}) translate(${-x-160} -408)"><rect x="${x}" y="255" width="320" height="305" rx="15" fill="${active?'#28251a':'#10100e'}" stroke="${active?YELLOW:'#555044'}" stroke-width="${active?3:1}"/>${board(item.asset,x+12,272,296,222)}</g>`;
  if(active)art+=bar(x+22,552,276,f.elapsed/f.length);
 });
 art+=`<g opacity="${.6+.4*ease(f.elapsed/.4)}">${centered(c.heading,960,659,45,65,YELLOW)}${centered(c.text,960,735,37,77)}</g>`;
 return {art,footer:noteFooter(`${String(f.index+1).padStart(2,'0')} / ${cues.length} — ${c.heading}`),badge:'MEET THE PARTS'};
}
function deviceScene(s,t){const f=phase(s.direction.actions,t,s.duration),a=f.item,ctrl=a.control,q=pressEnvelope(f.elapsed),turn=ease(f.elapsed/1.1),prev=s.direction.actions[Math.max(0,f.index-1)];
 const response=ctrl.endsWith('turn')?.85:.2;
 let screen=f.elapsed<response&&f.index?prev.settledScreen||prev.screen:a.screen;
 if(a.settledScreen&&f.elapsed>.34)screen=a.settledScreen;
 const pad=ctrl.startsWith('pad')?Number(ctrl.slice(3))-1:-1;
 const legends=PAD_LABELS;
 let art=`<g data-device="assembled" data-layout="vertical-photo-s"><rect x="690" y="218" width="560" height="668" rx="24" fill="#030303"/><rect x="680" y="201" width="560" height="675" rx="22" fill="url(#panel)" stroke="#6e6958" stroke-width="2"/><rect x="685" y="206" width="550" height="665" rx="20" filter="url(#powder)" opacity=".55"/><path d="M711 224h70" stroke="${YELLOW}" stroke-width="4"/>${tx('SAMPLOTRON',802,228,20,YELLOW)}<rect data-component="oled-bezel" x="748" y="237" width="424" height="214" rx="7" fill="#030303" stroke="#454135"/>`;
 art+=`<g data-component="oled">${oledMarkup(screen,768,247,384,192)}</g>`;
 art+=`<g data-component="encoders">${knob(828,493,31,'',0,ctrl==='left-turn'?turn*30:0)}${knob(1092,493,31,'',0,ctrl==='right-turn'?turn*(a.turn||1)*30:0)}</g>`;
 art+=`<g data-component="keypad">${keypad(800,552,320,pad,q,legends)}</g>`;
 if(pad>=0)art+=touch(834+pad%4*80,586+Math.floor(pad/4)*80,f.elapsed);
 if(ctrl.endsWith('turn')){const x=ctrl==='left-turn'?828:1092;art+=`<path d="M${x-44} 490 A47 47 0 0 1 ${x+17} 447" fill="none" stroke="${YELLOW}" stroke-width="3" opacity="${1-ease(f.elapsed/1.8)}"/>`;}
 for(const [x,y] of [[701,221],[1218,221],[701,853],[1218,853],[757,246],[1163,246],[757,441],[1163,441]])art+=`<circle cx="${x}" cy="${y}" r="5" fill="#918064"/><path d="M${x-3} ${y}h6" stroke="#121210" stroke-width="2"/>`;
 art+='</g>';
 for(const [label,y] of [['OLED',340],['TWO ENCODERS',496],['4 × 4 KEYPAD',700]])art+=tx(label,145,y,25,MUTED,24)+`<path d="M430 ${y-7}H640" stroke="#5a5032" stroke-width="2"/>`;
 art+=tx('DISPLAY AND CONTROLS',1320,362,21,YELLOW,25)+tx('Display above the encoders. Keypad below.',1320,408,20,MUTED,30)+bar(1320,587,410,f.elapsed/f.length);
 return {art,footer:noteFooter(a.gesture,true),badge:'WATCH IT RESPOND',concept:true};
}
function wiringScene(s,t){const lead=s.direction?.readSeconds||0,local=t-lead,n=s.wires.length,available=s.duration-lead-3,slot=available/n,index=Math.min(n-1,Math.floor(local/slot));let art=board(s.left,90,210,550,310)+board(s.right,1280,210,550,310);
 if(s.connectionSummary)art+=s.connectionSummary.map((line,i)=>centered(line,960,414+i*36,25,40,YELLOW)).join('')+centered('*Check OLED supply rating',960,532,19,35,MUTED);
 s.wires.forEach((w,i)=>{const elapsed=local-i*slot,draw=ease(elapsed/1.5),active=i===index,shown=elapsed>=0,y=580+i*62;
  art+=`<g data-wire="${i}" opacity="${shown?active?1:.55:.12}"><path d="M560 ${y} C800 ${y-24},1120 ${y+24},1360 ${y}" pathLength="1" fill="none" stroke="#040404" stroke-width="14"/><path d="M560 ${y} C800 ${y-24},1120 ${y+24},1360 ${y}" pathLength="1" fill="none" stroke="${w.color}" stroke-width="7" stroke-linecap="round" stroke-dasharray="1" stroke-dashoffset="${1-draw}"/><circle cx="560" cy="${y}" r="9" fill="${w.color}"/><circle cx="1360" cy="${y}" r="9" fill="${draw===1?w.color:'#494437'}"/>`;
  art+=tx(w.source,535,y+8,25,active?WHITE:MUTED,32).replace('<text ','<text text-anchor="end" ')+tx(w.target,1385,y+8,25,active?WHITE:MUTED,31)+'</g>';
  if(active&&elapsed>=1.5&&elapsed<2.3)art+=`<circle cx="1360" cy="${y}" r="${12+(elapsed-1.5)*18}" fill="none" stroke="${w.color}" stroke-width="3" opacity="${1-(elapsed-1.5)/.8}"/>`;
 });
 const complete=local>=available,w=s.wires[index];
 art+=centered(complete?'CHECK BOTH ENDS':`CONNECT ${index+1} / ${n}`,960,293,21,35,YELLOW);
 art+=bar(780,344,360,complete?(local-available)/3:(local-index*slot)/slot);
 const footer=complete?'Check the labels before powering on.':`${w.source} → ${w.target}`;
 return {art,footer:noteFooter(footer),badge:'CONNECT'};
}
function controlFromCue(c){return c.control||'none';}
function screenActionScene(s,t){const f=phase(s.screenCues,t,s.duration),c=f.item,ctrl=controlFromCue(c),hold=ctrl==='right-hold',q=pressEnvelope(f.elapsed,hold),turn=ease(f.elapsed/.9),delay=hold?.75:.2;
 if(c.readText){const view=readFirst({...s,caption:c.readText,direction:{readSeconds:f.length,mode:'guided'}},f.elapsed);return {...view,badge:'PANIC BUTTON'};}
 const prev=s.screenCues[Math.max(0,f.index-1)];let screen=f.index&&ctrl!=='none'&&f.elapsed<delay?prev.screen:c.screen;
 if(c.settledScreen&&f.elapsed>.34)screen=c.settledScreen;
 let art=`<rect x="569" y="223" width="1254" height="635" rx="17" fill="#050505" stroke="#454236" stroke-width="3"/><path d="M587 213 H761" stroke="${YELLOW}" stroke-width="5"/>`+oledMarkup(screen,620,249,1152,576);
 if(ctrl.startsWith('pad')){const pad=Number(ctrl.slice(3))-1;art+=keypad(140,285,320,pad,q,PAD_LABELS)+touch(174+pad%4*80,319+Math.floor(pad/4)*80,f.elapsed);}
 else if(ctrl==='midi-key'){art+=`<rect x="105" y="300" width="385" height="215" rx="12" fill="#11110f" stroke="#5a5340"/>`;for(let i=0;i<7;i++)art+=`<rect x="${120+i*51}" y="${315+(i===3?q*8:0)}" width="47" height="175" rx="5" fill="${i===3&&q?YELLOW:'#dbd5c3'}"/>`;art+=touch(297,420,f.elapsed);}
 else if(s.id==='oled-test'){art+=board('esp',78,265,440,270);}
 else{art+=knob(188,389,68,'LEFT',ctrl==='left-press'?q:0,ctrl==='left-turn'?turn*(c.turn||1)*30:0)+knob(420,389,68,'RIGHT',ctrl.startsWith('right-')&&!ctrl.endsWith('turn')?q:0,ctrl==='right-turn'?turn*30:0);
  if(ctrl==='left-press')art+=touch(188,389,f.elapsed);
  if(ctrl==='right-press'||hold)art+=touch(420,389,f.elapsed,hold);
  if(hold&&f.elapsed<1.2)art+=bar(360,510,120,Math.min(f.elapsed/.7,1));
 }
 art+=tx('DO THIS',100,620,20,YELLOW,30)+tx(c.gesture,100,665,28,WHITE,26)+bar(100,827,390,f.elapsed/f.length);
 return {art,footer:noteFooter(c.footer||(ctrl==='none'?'Observe the device screen.':hold?'Hold the button, then watch the screen change.':'Follow the highlighted control, then check the OLED.')),badge:'DO → OBSERVE'};
}
function directedFrame(s,t){const d=s.direction||{mode:'guided',readSeconds:0};
 if(d.readSeconds>0&&t<d.readSeconds)return d.introduction?welcomeScene(s,t):readFirst(s,t);
 if(d.mode==='device')return deviceScene(s,t);
 if(d.mode==='spotlight')return spotlightScene(s,t);
 if(d.mode==='cards')return cardsScene(s,t);
 if(s.wires.length)return wiringScene(s,t);
 if(s.screenCues?.length)return screenActionScene(s,t);
 return readFirst({...s,direction:{readSeconds:s.duration}},t);
}
function paintDirectedFrame(s,t,index){const v=directedFrame(s,t);
 const titleSize=s.title.length>46?44:53;
 return steel()+`<rect x="72" y="67" width="43" height="6" fill="${YELLOW}"/>${tx('SAMPLOTRON / BUILD GUIDE',134,80,22,'#c3bdab')}${tx(`${String(index+1).padStart(2,'0')} / ${project.scenes.length}`,1810,80,24,MUTED).replace('<text ','<text text-anchor="end" ')}${tx(s.title,72,160,titleSize,WHITE,70)}${v.art}${v.footer}<rect x="0" y="1075" width="${1920*t/s.duration}" height="5" fill="${YELLOW}"/>${tx(v.credit??(v.concept?'ASSEMBLED LAYOUT · ILLUSTRATION':'ILLUSTRATIVE WIRING · VERIFY YOUR BOARD REVISION'),1810,880,15,MUTED).replace('<text ','<text text-anchor="end" ')}${tx(v.badge,90,1041,17,YELLOW,30)}`;
}

return Object.freeze({render:paintDirectedFrame});
})();
