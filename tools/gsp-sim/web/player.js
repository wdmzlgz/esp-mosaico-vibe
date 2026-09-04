(() => {
  const status = document.getElementById("status");
  const canvas = document.getElementById("canvas");
  const context = canvas.getContext("2d");

  const setStatus = (text) => {
    status.textContent = text;
  };

  const align64 = (pointer) => (pointer + 63) & ~63;

  const rgb565ToImageData = (heap, pointer, width, height, stride) => {
    const image = context.createImageData(width, height);
    const dest = image.data;
    for (let y = 0; y < height; y += 1) {
      const srcRow = pointer + y * stride;
      const destRow = y * width * 4;
      for (let x = 0; x < width; x += 1) {
        const packed = heap[srcRow + x * 2] | (heap[srcRow + x * 2 + 1] << 8);
        const destIndex = destRow + x * 4;
        dest[destIndex] = ((packed >> 11) & 31) * 255 / 31;
        dest[destIndex + 1] = ((packed >> 5) & 63) * 255 / 63;
        dest[destIndex + 2] = (packed & 31) * 255 / 31;
        dest[destIndex + 3] = 255;
      }
    }
    return image;
  };

  const canvasPoint = (event) => {
    const rect = canvas.getBoundingClientRect();
    const x = Math.round((event.clientX - rect.left) * canvas.width / rect.width);
    const y = Math.round((event.clientY - rect.top) * canvas.height / rect.height);
    return {
      x: Math.max(0, Math.min(canvas.width - 1, x)),
      y: Math.max(0, Math.min(canvas.height - 1, y)),
    };
  };

  const loadBackend = async () => {
    const response = await fetch("backend.json", { cache: "no-store" });
    if (response.status === 404) return {};
    if (!response.ok) {
      throw new Error(`failed to load backend.json (${response.status})`);
    }
    return response.json();
  };

  const start = async () => {
    if (typeof GspSimModule !== "function") {
      throw new Error("gsp_sim.js did not export GspSimModule");
    }
    const module = await GspSimModule();
    const response = await fetch("preview.gspb", { cache: "no-store" });
    if (!response.ok) {
      throw new Error(`failed to load preview.gspb (${response.status})`);
    }
    const bytes = new Uint8Array(await response.arrayBuffer());
    const raw = module._malloc(bytes.length + 64);
    const aligned = align64(raw);
    module.HEAPU8.set(bytes, aligned);
    const rc = module._gsp_sim_init(aligned, bytes.length);
    if (rc !== 0) {
      throw new Error(`gsp_sim_init failed (${rc})`);
    }

    const width = module._gsp_sim_width();
    const height = module._gsp_sim_height();
    canvas.width = width;
    canvas.height = height;
    setStatus(`${width}×${height} WebAssembly preview`);

    const backend = await loadBackend();
    const timers = (backend.timers || []).map((timer) => ({
      bindId: Number(timer.bind_id),
      period: Number(timer.period_ms) || 250,
      step: Number(timer.step) || 1,
      modulo: Number(timer.modulo) || 101,
      value: Number(timer.start) || 0,
      due: performance.now() + (Number(timer.period_ms) || 250),
    }));

    const feed = (event, pressed) => {
      const point = canvasPoint(event);
      module._gsp_sim_feed_pointer(point.x, point.y, pressed ? 1 : 0);
    };
    canvas.addEventListener("pointerdown", (event) => {
      canvas.setPointerCapture(event.pointerId);
      feed(event, true);
    });
    canvas.addEventListener("pointermove", (event) => {
      if (event.buttons) feed(event, true);
    });
    const release = (event) => feed(event, false);
    canvas.addEventListener("pointerup", release);
    canvas.addEventListener("pointercancel", release);

    let last = performance.now();
    const tick = (now) => {
      const delta = Math.max(1, Math.min(100, now - last));
      last = now;
      for (const timer of timers) {
        if (now < timer.due) continue;
        timer.value = (timer.value + timer.step) % timer.modulo;
        module._gsp_sim_set_value(timer.bindId, timer.value);
        timer.due = now + timer.period;
      }
      const step = module._gsp_sim_step(delta);
      if (step === 0) {
        const pixels = module._gsp_sim_pixels();
        const stride = module._gsp_sim_stride();
        if (pixels > 0 && stride > 0) {
          context.putImageData(
            rgb565ToImageData(module.HEAPU8, pixels, width, height, stride),
            0,
            0,
          );
        }
      }
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  };

  start().catch((error) => {
    console.error(error);
    setStatus(`Failed to start: ${error.message || error}`);
  });
})();
