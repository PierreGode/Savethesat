/* Savethesat web flasher — esptool-js over Web Serial.
 *
 * Adapted from the Ragnar / Piglet flasher (MIT).
 * Flashes a single merged image at offset 0; the GitHub Actions workflow
 * builds bootloader + partitions + boot_app0 + app into that one file.
 */

let _esptool = null;
async function getEsptool() {
  if (_esptool) return _esptool;
  _esptool = await import("https://unpkg.com/esptool-js@0.6.0/bundle.js");
  return _esptool;
}

const $ = (id) => document.getElementById(id);
const log = (m) => {
  const el = $("flash-log");
  el.textContent += m + "\n";
  el.scrollTop = el.scrollHeight;
};
const setStatus = (m) => { $("flash-status").textContent = m; };
const setProgress = (p) => {
  $("flash-bar").style.width = p + "%";
  $("flash-pct").textContent = Math.round(p) + "%";
};

function openOverlay() {
  $("flash-overlay").hidden = false;
  $("flash-close").hidden = true;
  $("flash-confirm-row").hidden = true;
  $("flash-log").textContent = "";
  setProgress(0);
  $("flash-pct").textContent = "";
  setStatus("Initializing…");
}

function waitForConfirm() {
  return new Promise((resolve) => {
    $("flash-confirm-row").hidden = false;
    $("flash-go").onclick     = () => { $("flash-confirm-row").hidden = true; resolve(true); };
    $("flash-cancel").onclick = () => { $("flash-confirm-row").hidden = true; resolve(false); };
  });
}

window.flashDevice = async function (manifestPath) {
  if (!("serial" in navigator)) {
    alert("This browser has no Web Serial support.\n\nUse desktop Chrome, Edge or Opera. " +
          "Safari, Firefox, and all phone browsers cannot flash.");
    return;
  }

  openOverlay();
  let transport = null;

  try {
    setStatus("Fetching firmware manifest…");
    const mResp = await fetch(manifestPath, { cache: "no-store" });
    if (!mResp.ok) throw new Error("Manifest fetch failed (" + mResp.status + ")");
    const manifest = await mResp.json();
    const build = manifest.builds[0];
    const parts = build.parts || [];
    if (!parts.length) throw new Error("Manifest lists no firmware parts");

    log("Firmware : " + manifest.name);
    log("Target   : " + build.chipFamily);
    log("Version  : " + (manifest.version || "—"));
    log("");

    setStatus("Select the serial port…");
    let port;
    try {
      port = await navigator.serial.requestPort();
    } catch (_) {
      setStatus("No port selected.");
      $("flash-close").hidden = false;
      return;
    }

    setStatus("Loading flasher…");
    const { ESPLoader, Transport } = await getEsptool();

    setStatus("Downloading firmware…");
    const base = manifestPath.substring(0, manifestPath.lastIndexOf("/") + 1);
    const fileArray = [];
    let totalBytes = 0;
    for (const part of parts) {
      const url = part.path.startsWith("http") ? part.path : base + part.path;
      const resp = await fetch(url, { cache: "no-store" });
      if (!resp.ok) throw new Error("Download failed: " + part.path + " (" + resp.status + ")");
      const data = new Uint8Array(await resp.arrayBuffer());
      fileArray.push({ data, address: part.offset });
      totalBytes += data.length;
      log("  " + part.path.split("/").pop() + " @ 0x" + part.offset.toString(16) +
          "  (" + (data.length / 1024).toFixed(0) + " KB)");
    }

    setStatus("Connecting…");
    transport = new Transport(port, true);
    const terminal = { clean() {}, writeLine(d) { log(d); }, write() {} };
    const loader = new ESPLoader({ transport, baudrate: 921600, romBaudrate: 115200, terminal });
    const chip = await loader.main();
    log("\nConnected: " + chip);

    const norm = (s) => s.replace(/[-_ ]/g, "").toUpperCase();
    if (!norm(chip).startsWith(norm(build.chipFamily)))
      throw new Error("Wrong chip. Expected " + build.chipFamily + ", found " + chip + ".");

    setStatus("Ready — confirm to erase and flash");
    log("Device verified. Flashing will erase everything on this board.\n");
    if (!(await waitForConfirm())) {
      setStatus("Cancelled.");
      await transport.disconnect();
      $("flash-close").hidden = false;
      return;
    }

    if (manifest.new_install_prompt_erase !== false) {
      setStatus("Erasing flash…");
      log("Erasing…");
      await loader.eraseFlash();
      log("Erase complete.");
    }

    setStatus("Writing firmware…");
    const prior = [];
    let acc = 0;
    for (const f of fileArray) { prior.push(acc); acc += f.data.length; }

    await loader.writeFlash({
      fileArray,
      flashSize: "keep",
      flashMode: "keep",
      flashFreq: "keep",
      eraseAll: false,
      compress: true,
      reportProgress(i, written, total) {
        const done = (prior[i] || 0) + (written / total) * fileArray[i].data.length;
        setProgress((done / totalBytes) * 100);
      },
    });

    setProgress(100);
    log("\nFlash complete.");
    setStatus("Done — unplug and replug the board, then join its Wi-Fi.");
    await transport.disconnect();
    transport = null;
  } catch (err) {
    log("\nERROR: " + (err && err.message ? err.message : err));
    setStatus("Failed.");
    if (transport) { try { await transport.disconnect(); } catch (_) {} }
  } finally {
    $("flash-close").hidden = false;
  }
};

window.closeOverlay = () => { $("flash-overlay").hidden = true; };

document.addEventListener("DOMContentLoaded", () => {
  if (!("serial" in navigator)) {
    const b = $("flash-btn");
    b.disabled = true;
    b.textContent = "Needs desktop Chrome or Edge";
    $("browser-warn").hidden = false;
  }
});
