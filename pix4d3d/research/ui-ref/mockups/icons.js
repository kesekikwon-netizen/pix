// Lucide 계열 24px 선 아이콘(획 2, 둥근 끝) — 목업 전용
const IC={
 open:'<path d="M3 7h6l2 2h10v10H3z"/>',
 recent:'<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
 draw:'<path d="M4 20 20 4"/><circle cx="4" cy="20" r="2"/><circle cx="20" cy="4" r="2"/>',
 flip:'<path d="M7 4v16M3 8l4-4 4 4M17 20V4M13 16l4 4 4-4"/>',
 nudge:'<path d="M4 12h16M8 8l-4 4 4 4M16 8l4 4-4 4"/>',
 list:'<path d="M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01"/>',
 image:'<rect x="3" y="5" width="18" height="14" rx="2"/><path d="m3 17 5-5 4 4 3-3 6 6"/>',
 cut:'<path d="M3 13c3 0 3-4 6-4s3 6 6 6 3-3 6-3"/>',
 levels:'<path d="M3 6h18M3 10h18M3 14h18M3 18h18"/>',
 measure:'<path d="M3 17 17 3l4 4L7 21z"/><path d="m7 13 2 2M10 10l2 2M13 7l2 2"/>',
 preview:'<rect x="5" y="3" width="14" height="18" rx="1"/><path d="M8 8h8M8 12h8M8 16h5"/>',
 dxf:'<path d="M14 3H6v18h12V7z"/><path d="M14 3v4h4"/><path d="M9 13h6M9 17h4"/>',
 png:'<rect x="3" y="3" width="18" height="18" rx="2"/><circle cx="9" cy="9" r="2"/><path d="m21 15-5-5L5 21"/>',
 fit:'<path d="M3 8V3h5M16 3h5v5M21 16v5h-5M8 21H3v-5"/>',
 undo:'<path d="M9 14 4 9l5-5"/><path d="M4 9h11a5 5 0 0 1 0 10h-3"/>',
 redo:'<path d="m15 14 5-5-5-5"/><path d="M20 9H9a5 5 0 0 0 0 10h3"/>',
 datum:'<path d="M12 3v18M7 8l5-5 5 5"/><path d="M4 21h16"/>',
 plus:'<path d="M12 5v14M5 12h14"/>',
 zin:'<circle cx="11" cy="11" r="7"/><path d="m20 20-4-4M8 11h6M11 8v6"/>',
 zout:'<circle cx="11" cy="11" r="7"/><path d="m20 20-4-4M8 11h6"/>',
 more:'<circle cx="5" cy="12" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="19" cy="12" r="1"/>',
 eye:'<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
 max:'<rect x="4" y="4" width="16" height="16" rx="1"/>',
 print:'<path d="M6 9V3h12v6"/><rect x="3" y="9" width="18" height="8" rx="1"/><path d="M6 14h12v7H6z"/>',
};
function ic(n){return `<svg viewBox="0 0 24 24">${IC[n]}</svg>`}
document.querySelectorAll('[data-ic]').forEach(e=>e.insertAdjacentHTML('afterbegin',ic(e.dataset.ic)));
// 레벨선(맨 뒤) — 10 cm 선, 50 cm 숫자
function levels(svg,opt){
 const {w,h,ppm,zTop,x0=0,labelL=true,labelR=true,minor=0.1,major=0.5}=opt;
 let s='';
 const zMin=zTop-h/ppm;
 for(let z=Math.ceil(zMin/minor)*minor; z<=zTop+1e-9; z+=minor){
   const zz=Math.round(z*10)/10, y=(zTop-zz)*ppm, isMaj=Math.abs(zz/major-Math.round(zz/major))<1e-6;
   s+=`<line x1="${x0}" x2="${w}" y1="${y}" y2="${y}" stroke="${isMaj?'#9C9A92':'#DEDCD1'}" stroke-width="${isMaj?1:0.75}"/>`;
   if(isMaj){
     if(labelL) s+=`<text x="${x0-6}" y="${y+4}" text-anchor="end" font-family="Consolas,JetBrains Mono,monospace" font-size="11" fill="#3D3D3A">${zz.toFixed(1)}</text>`;
     if(labelR) s+=`<text x="${w+6}" y="${y+4}" font-family="Consolas,JetBrains Mono,monospace" font-size="11" fill="#3D3D3A">${zz.toFixed(1)}</text>`;
   }
 }
 svg.insertAdjacentHTML('afterbegin',s);
}
