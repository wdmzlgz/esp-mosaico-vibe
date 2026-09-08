// Run against a live gsp_sub_board WASM preview; requires Playwright.
const { chromium }=require('playwright');
const assert=require('node:assert/strict');
(async()=>{
const browser=await chromium.launch({headless:true,executablePath:process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE,args:['--no-sandbox','--use-fake-device-for-media-stream','--use-fake-ui-for-media-stream']});
const page=await browser.newPage({viewport:{width:1800,height:1000}});
page.on('pageerror',e=>console.log('PAGE ERROR',e.message));
const errors=[];
page.on('console',m=>{ if(m.text().includes('E (gsp_')) errors.push(m.text()); });
page.on('requestfailed',r=>console.log('requestfailed',r.url(),r.failure()));
await page.goto(process.env.SUB_BOARD_URL || 'http://127.0.0.1:8877/');
await page.locator('#sim-power-key').click();
await page.waitForTimeout(1500);
console.log('STATE',await page.evaluate(()=>({camera:!!window.SubBoardCamera,armed:Module._gsp_app_sim_gpio_armed?.(7)})));
await page.waitForTimeout(500);
await page.screenshot({path:'/tmp/sub-board-empty.png'});
await page.evaluate(()=>{MosaicoSideboards.setBoard('left','ws2812');MosaicoSideboards.setBoard('right','ws2812');});
await page.waitForTimeout(700);
await page.locator('#canvas').click({position:{x:220,y:250}});
await page.waitForTimeout(700);
assert.equal(await page.locator('.ws2812-cell.lit').count(),128); console.log('Both matrices lit: PASS');
await page.waitForFunction(() => {
  const levels = [...document.querySelectorAll('.ws2812-cell.lit')]
    .map(cell => Number(cell.style.getPropertyValue('--led-level')) || 0);
  return levels.length > 0 && Math.max(...levels) >= 0.65;
});
const renderedColors = await page.locator('.ws2812-cell.lit').evaluateAll(cells =>
  cells.map(cell => cell.style.getPropertyValue('--led-color')));
assert.ok(new Set(renderedColors).size > 32);
console.log('Browser additive light is bright and varied: PASS');
await page.screenshot({path:'/tmp/sub-board-lights.png'});
await page.locator('#gpio7-key').click({delay:120});
await page.waitForTimeout(150);
assert.equal(await page.locator('.ws2812-cell.lit').count(),0); console.log('GPIO7 stops lights: PASS');
assert.equal(await page.locator('.ws2812-cell[style*="--led-level"]').count(),0);
await page.evaluate(()=>{MosaicoSideboards.setBoard('left','camera');MosaicoSideboards.clearBoard('right');});
await page.waitForTimeout(600);
await page.locator('#canvas').click({position:{x:220,y:180}});
await page.waitForFunction(()=>SubBoardCamera.video?.readyState>=2,null,{timeout:15000});
console.log('camera',await page.evaluate(()=>({w:SubBoardCamera.video.videoWidth,h:SubBoardCamera.video.videoHeight,tracks:SubBoardCamera.stream.getTracks().length})));
await page.waitForTimeout(500);
await page.screenshot({path:'/tmp/sub-board-camera.png'});
await page.evaluate(()=>MosaicoSideboards.clearBoard('left'));
await page.waitForTimeout(300);
assert.equal(await page.evaluate(()=>SubBoardCamera.stream===null),true);
console.log('Camera removal stops tracks: PASS');
// GPIO7 also releases an active camera, then a fresh start works.
await page.evaluate(()=>MosaicoSideboards.setBoard('left','camera'));
await page.waitForTimeout(200);
await page.locator('#canvas').click({position:{x:220,y:180}});
await page.waitForFunction(()=>SubBoardCamera.video?.readyState>=2);
await page.evaluate(()=>window.oldTracks=SubBoardCamera.stream.getTracks());
await page.locator('#gpio7-key').click({delay:120});
await page.waitForTimeout(150);
assert.equal(await page.evaluate(()=>oldTracks.every(t=>t.readyState==='ended')),true);
console.log('GPIO7 releases active camera: PASS');
// Permission rejection is visible and does not leave a live stream.
await page.evaluate(()=>{
  window.originalGetUserMedia=navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);
  navigator.mediaDevices.getUserMedia=()=>Promise.reject(new DOMException('Denied','NotAllowedError'));
});
await page.locator('#canvas').click({position:{x:220,y:180}});
await page.waitForTimeout(300);
assert.equal(await page.evaluate(()=>SubBoardCamera.stream===null),true);
await page.screenshot({path:'/tmp/sub-board-denied.png'});
console.log('Permission denial returns home: PASS');
// A permission result arriving after physical removal must be discarded.
await page.evaluate(()=>{
  navigator.mediaDevices.getUserMedia=async()=>{
    const stream=await originalGetUserMedia({video:true});
    window.delayedTracks=stream.getTracks();
    await new Promise(resolve=>setTimeout(resolve,700));
    return stream;
  };
});
await page.locator('#canvas').click({position:{x:220,y:180}});
await page.waitForFunction(()=>window.delayedTracks);
await page.evaluate(()=>MosaicoSideboards.clearBoard('left'));
await page.waitForTimeout(900);
assert.equal(await page.evaluate(()=>delayedTracks.every(t=>t.readyState==='ended')),true);
console.log('Late camera permission after removal: PASS');
assert.deepEqual(errors,[]);
await browser.close();
})().catch(e=>{console.error(e);process.exit(1)});
