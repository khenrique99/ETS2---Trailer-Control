// Gera app/app.ico (16, 32, 48, 256 px) sem dependencias: quadrado azul arredondado com
// dois "engates" (circulos) ligados. Uso: node scripts/make_icon.js
const fs = require('fs');
const path = require('path');

function render(size) {
  const px = new Uint8Array(size * size * 4); // BGRA, linha de baixo primeiro (BMP)
  const s = size / 32;
  const put = (x, y, b, g, r, a) => {
    const i = ((size - 1 - y) * size + x) * 4;
    // composicao "over" simples
    const ia = px[i + 3] / 255, na = a / 255, oa = na + ia * (1 - na);
    if (oa === 0) return;
    px[i] = Math.round((b * na + px[i] * ia * (1 - na)) / oa);
    px[i + 1] = Math.round((g * na + px[i + 1] * ia * (1 - na)) / oa);
    px[i + 2] = Math.round((r * na + px[i + 2] * ia * (1 - na)) / oa);
    px[i + 3] = Math.round(oa * 255);
  };
  // cobertura com 4x4 amostras por pixel
  const cover = (x, y, inside) => {
    let c = 0;
    for (let sy = 0; sy < 4; sy++) for (let sx = 0; sx < 4; sx++) if (inside(x + (sx + 0.5) / 4, y + (sy + 0.5) / 4)) c++;
    return c / 16;
  };
  const rr = (x, y, x0, y0, x1, y1, rad) => {
    const cx = Math.min(Math.max(x, x0 + rad), x1 - rad), cy = Math.min(Math.max(y, y0 + rad), y1 - rad);
    return (x - cx) ** 2 + (y - cy) ** 2 <= rad * rad && x >= x0 && x <= x1 && y >= y0 && y <= y1;
  };
  const circ = (cx, cy, r) => (x, y) => (x - cx) ** 2 + (y - cy) ** 2 <= r * r;
  const shapes = [
    [(x, y) => rr(x, y, 1 * s, 1 * s, 31 * s, 31 * s, 7 * s), [235, 99, 37]],        // fundo azul
    [(x, y) => rr(x, y, 6 * s, 14.5 * s, 26 * s, 17.5 * s, 1.5 * s), [255, 255, 255]], // barra
    [circ(9 * s, 16 * s, 5 * s), [255, 255, 255]],
    [circ(23 * s, 16 * s, 5 * s), [255, 255, 255]],
    [circ(9 * s, 16 * s, 2.2 * s), [235, 99, 37]],
    [circ(23 * s, 16 * s, 2.2 * s), [55, 127, 26]],                                     // verde = engatado
  ];
  for (let y = 0; y < size; y++)
    for (let x = 0; x < size; x++)
      for (const [fn, [b, g, r]] of shapes) {
        const c = cover(x, y, fn);
        if (c > 0) put(x, y, b, g, r, Math.round(c * 255));
      }
  return px;
}

function bmpEntry(size) {
  const px = render(size);
  const header = Buffer.alloc(40);
  header.writeUInt32LE(40, 0);
  header.writeInt32LE(size, 4);
  header.writeInt32LE(size * 2, 8); // cor + mascara
  header.writeUInt16LE(1, 12);
  header.writeUInt16LE(32, 14);
  const maskRow = Math.ceil(size / 32) * 4;
  return Buffer.concat([header, Buffer.from(px), Buffer.alloc(maskRow * size)]);
}

const sizes = [16, 32, 48, 256];
const images = sizes.map(bmpEntry);
const dir = Buffer.alloc(6 + 16 * sizes.length);
dir.writeUInt16LE(0, 0);
dir.writeUInt16LE(1, 2);
dir.writeUInt16LE(sizes.length, 4);
let offset = dir.length;
sizes.forEach((sz, i) => {
  const e = 6 + i * 16;
  dir.writeUInt8(sz >= 256 ? 0 : sz, e);
  dir.writeUInt8(sz >= 256 ? 0 : sz, e + 1);
  dir.writeUInt16LE(1, e + 4);
  dir.writeUInt16LE(32, e + 6);
  dir.writeUInt32LE(images[i].length, e + 8);
  dir.writeUInt32LE(offset, e + 12);
  offset += images[i].length;
});
const out = path.join(__dirname, '..', 'app', 'app.ico');
fs.writeFileSync(out, Buffer.concat([dir, ...images]));
console.log('ok', out);
