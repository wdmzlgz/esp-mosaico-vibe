(() => {
  const STORAGE_KEY = "mosaico-sideboards";
  const BOARDS = {
    camera: {
      asset: "assets/camera-board-clean.png",
      sides: new Set(["left"]),
    },
    ws2812: {
      asset: "assets/ws2812-board-unlit.png",
      sides: new Set(["left", "right"]),
    },
  };

  const slots = {
    left: document.querySelector('[data-side-module="left"]'),
    right: document.querySelector('[data-side-module="right"]'),
  };
  const state = { left: "", right: "" };

  const saveState = () => {
    try {
      sessionStorage.setItem(STORAGE_KEY, JSON.stringify(state));
    } catch (error) {
      console.warn("Unable to save sideboard state", error);
    }
  };

  const loadState = () => {
    try {
      const saved = JSON.parse(sessionStorage.getItem(STORAGE_KEY) || "{}");
      return {
        left: typeof saved.left === "string" ? saved.left : "",
        right: typeof saved.right === "string" ? saved.right : "",
      };
    } catch (error) {
      console.warn("Unable to restore sideboard state", error);
      return { left: "", right: "" };
    }
  };

  const normalizeColor = (color) => {
    if (!color) return "";
    if (Array.isArray(color)) {
      const [r, g, b] = color.map((value) =>
        Math.max(0, Math.min(255, Number(value) || 0))
      );
      return `rgb(${r}, ${g}, ${b})`;
    }
    return String(color);
  };

  const updateButtons = () => {
    document.querySelectorAll("[data-sideboard-set]").forEach((button) => {
      const pressed =
        state[button.dataset.sideboardSide] === button.dataset.sideboardSet;
      button.setAttribute("aria-pressed", pressed ? "true" : "false");
    });
  };

  const setBoard = (side, board) => {
    if (!slots[side]) return false;
    if (!board) {
      delete document.body.dataset[`${side}Board`];
      slots[side].dataset.board = "";
      slots[side].querySelector("[data-sideboard-art]").removeAttribute("src");
      state[side] = "";
      updateButtons();
      saveState();
      return true;
    }
    const spec = BOARDS[board];
    if (!spec || !spec.sides.has(side)) return false;
    document.body.dataset[`${side}Board`] = board;
    slots[side].dataset.board = board;
    slots[side].querySelector("[data-sideboard-art]").src = spec.asset;
    state[side] = board;
    updateButtons();
    saveState();
    return true;
  };

  const restoreBoards = () => {
    const saved = loadState();
    setBoard("left", saved.left);
    setBoard("right", saved.right);
  };

  const createGrid = (grid) => {
    grid.textContent = "";
    for (let index = 0; index < 64; index += 1) {
      const cell = document.createElement("i");
      cell.className = "ws2812-cell";
      cell.dataset.index = String(index);
      grid.appendChild(cell);
    }
  };

  const setWs2812Pixel = (side, row, column, color, level = 1) => {
    const slot = slots[side];
    if (!slot) return false;
    const r = Number(row);
    const c = Number(column);
    if (!Number.isInteger(r) || !Number.isInteger(c) || r < 0 || r > 7 || c < 0 || c > 7) {
      return false;
    }
    const cell = slot.querySelector(`[data-index="${r * 8 + c}"]`);
    if (!cell) return false;
    const cssColor = normalizeColor(color);
    const lightLevel = Math.max(0, Math.min(1, Number(level) || 0));
    if (!cssColor || cssColor === "off" || cssColor === "transparent" || lightLevel === 0) {
      cell.classList.remove("lit");
      cell.style.removeProperty("--led-color");
      cell.style.removeProperty("--led-level");
      return true;
    }
    cell.style.setProperty("--led-color", cssColor);
    cell.style.setProperty("--led-level", String(lightLevel));
    cell.classList.add("lit");
    return true;
  };

  const clearWs2812 = (side) => {
    const slot = slots[side];
    if (!slot) return false;
    slot.querySelectorAll(".ws2812-cell").forEach((cell) => {
      cell.classList.remove("lit");
      cell.style.removeProperty("--led-color");
      cell.style.removeProperty("--led-level");
    });
    return true;
  };

  const setWs2812Matrix = (side, matrix) => {
    if (!Array.isArray(matrix)) return false;
    for (let row = 0; row < 8; row += 1) {
      for (let column = 0; column < 8; column += 1) {
        const value = Array.isArray(matrix[row])
          ? matrix[row][column]
          : matrix[row * 8 + column];
        setWs2812Pixel(side, row, column, value);
      }
    }
    return true;
  };

  document.querySelectorAll("[data-ws2812-grid]").forEach(createGrid);
  document.querySelectorAll("[data-sideboard-set]").forEach((button) => {
    button.addEventListener("click", () => {
      const side = button.dataset.sideboardSide;
      const board = button.dataset.sideboardSet;
      setBoard(side, state[side] === board ? "" : board);
    });
  });

  window.MosaicoSideboards = {
    setBoard,
    clearBoard: (side) => setBoard(side, ""),
    getState: () => ({ ...state }),
    setWs2812Pixel,
    setWs2812Matrix,
    clearWs2812,
  };

  restoreBoards();
})();
