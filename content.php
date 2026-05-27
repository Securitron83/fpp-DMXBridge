<?php
// No server-side state needed — all communication goes through FPP's command API.
?>

<div class="container-fluid">

    <!-- Overview -->
    <div class="card card-outline card-primary">
        <div class="card-header">
            <h3 class="card-title"><i class="fas fa-sliders-h"></i> DMX Bridge — Channel Control</h3>
        </div>
        <div class="card-body">
            <p class="text-muted small mb-0">
                Set individual DMX channel values from the browser or from FPP command presets.
                Overrides are active only while <strong>no sequence or playlist is playing</strong>;
                starting a show hands full control back to the sequence.
                Use this to set default positions for moving heads, hold a static scene, etc.
            </p>
        </div>
    </div>

    <!-- Set Channel -->
    <div class="card card-outline card-warning mt-3">
        <div class="card-header">
            <h3 class="card-title"><i class="fas fa-paper-plane"></i> Set Channel</h3>
        </div>
        <div class="card-body">
            <div class="form-group row mb-2">
                <label class="col-sm-2 col-form-label col-form-label-sm"><strong>Channel</strong></label>
                <div class="col-sm-10">
                    <input type="number" id="dmxChannel" class="form-control form-control-sm"
                           style="max-width:120px;display:inline-block"
                           min="1" max="512" value="1">
                    <span class="text-muted small ml-2">1–512 (DMX channel)</span>
                </div>
            </div>
            <div class="form-group row mb-2">
                <label class="col-sm-2 col-form-label col-form-label-sm"><strong>Value</strong></label>
                <div class="col-sm-10 d-flex align-items-center" style="gap:10px">
                    <input type="range" id="dmxSlider" min="0" max="255" value="0"
                           style="width:200px" oninput="$('#dmxValue').val(this.value)">
                    <input type="number" id="dmxValue" class="form-control form-control-sm"
                           style="width:80px" min="0" max="255" value="0"
                           oninput="$('#dmxSlider').val(this.value)">
                    <span class="text-muted small">0–255</span>
                </div>
            </div>
            <div class="form-group row">
                <div class="col-sm-10 offset-sm-2">
                    <button class="btn btn-warning btn-sm" onclick="sendSetChannel()">
                        <i class="fas fa-paper-plane"></i> Send
                    </button>
                    <button class="btn btn-secondary btn-sm ml-2" onclick="sendClearAll()">
                        <i class="fas fa-times-circle"></i> Clear All
                    </button>
                    <span id="dmxStatus" class="ml-3 small"></span>
                </div>
            </div>
        </div>
    </div>

    <!-- Quick Presets -->
    <div class="card card-outline card-info mt-3">
        <div class="card-header">
            <h3 class="card-title"><i class="fas fa-bolt"></i> Quick Presets</h3>
            <div class="card-tools">
                <button type="button" class="btn btn-tool" data-card-widget="collapse">
                    <i class="fas fa-minus"></i>
                </button>
            </div>
        </div>
        <div class="card-body">
            <p class="text-muted small mb-2">
                Common values for a selected channel. Click to send immediately.
            </p>
            <div class="form-group row mb-2">
                <label class="col-sm-2 col-form-label col-form-label-sm"><strong>Channel</strong></label>
                <div class="col-sm-10">
                    <input type="number" id="presetChannel" class="form-control form-control-sm"
                           style="max-width:120px;display:inline-block"
                           min="1" max="512" value="1">
                </div>
            </div>
            <div class="row">
                <div class="col-sm-10 offset-sm-2" style="display:flex;flex-wrap:wrap;gap:8px">
                    <button class="btn btn-sm btn-outline-secondary" onclick="sendPreset(0)">0 — Off / Full CW</button>
                    <button class="btn btn-sm btn-outline-primary"   onclick="sendPreset(64)">64 — 25%</button>
                    <button class="btn btn-sm btn-outline-primary"   onclick="sendPreset(128)">128 — Centre (50%)</button>
                    <button class="btn btn-sm btn-outline-primary"   onclick="sendPreset(192)">192 — 75%</button>
                    <button class="btn btn-sm btn-outline-danger"    onclick="sendPreset(255)">255 — Full / Full CCW</button>
                </div>
            </div>
            <div class="mt-2">
                <span id="presetStatus" class="small"></span>
            </div>
        </div>
    </div>

    <!-- How to use in command presets -->
    <div class="card card-outline card-secondary mt-3">
        <div class="card-header">
            <h3 class="card-title"><i class="fas fa-info-circle"></i> Using in Sequences &amp; Playlists</h3>
            <div class="card-tools">
                <button type="button" class="btn btn-tool" data-card-widget="collapse">
                    <i class="fas fa-minus"></i>
                </button>
            </div>
        </div>
        <div class="card-body">
            <p class="mb-2">Two FPP commands are available under <strong>Sequences &rarr; Command Presets</strong>:</p>
            <table class="table table-sm table-bordered" style="max-width:680px">
                <thead class="thead-light">
                    <tr><th>Command</th><th>Arguments</th><th>Notes</th></tr>
                </thead>
                <tbody>
                    <tr>
                        <td><code>DMX Bridge - Set Channel</code></td>
                        <td>Channel (1–512), Value (0–255)</td>
                        <td>Sets channel to value. Persists until the next Set Channel or Clear All command, or until a sequence starts.</td>
                    </tr>
                    <tr>
                        <td><code>DMX Bridge - Clear All</code></td>
                        <td><em>none</em></td>
                        <td>Zeroes all overrides, returning control entirely to the sequence player.</td>
                    </tr>
                </tbody>
            </table>
            <p class="text-muted small mb-0">
                <strong>Tip:</strong> Add a <em>Command</em> playlist entry at the very end of your show playlist
                (or in the Lead-Out) that fires <code>DMX Bridge - Set Channel</code> with your desired home
                position — e.g. channel&nbsp;1&nbsp;=&nbsp;128 to centre a pan axis.
                The override takes effect the moment the playlist stops playing.
            </p>
        </div>
    </div>

</div>

<script>
function apiCommand(cmd, args) {
    // Build the FPP command run URL: /api/command/CommandName/arg1/arg2/...
    const parts = ['/api/command', encodeURIComponent(cmd)].concat(args.map(encodeURIComponent));
    return fetch(parts.join('/'), { method: 'GET' })
        .then(r => r.ok ? r.json() : Promise.reject(r.status));
}

function setStatus(id, ok, msg) {
    $('#' + id).html(ok
        ? '<span class="text-success"><i class="fas fa-check"></i> ' + msg + '</span>'
        : '<span class="text-danger"><i class="fas fa-times"></i> ' + msg + '</span>'
    );
}

function sendSetChannel() {
    const ch  = parseInt($('#dmxChannel').val(), 10);
    const val = parseInt($('#dmxValue').val(), 10);
    if (isNaN(ch) || ch < 1 || ch > 512) { setStatus('dmxStatus', false, 'Invalid channel'); return; }
    if (isNaN(val) || val < 0 || val > 255)  { setStatus('dmxStatus', false, 'Invalid value');   return; }
    setStatus('dmxStatus', true, 'Sending…');
    apiCommand('DMX Bridge - Set Channel', [String(ch), String(val)])
        .then(() => setStatus('dmxStatus', true, 'Sent ch ' + ch + ' = ' + val))
        .catch(() => setStatus('dmxStatus', false, 'Failed — is the plugin loaded?'));
}

function sendClearAll() {
    apiCommand('DMX Bridge - Clear All', [])
        .then(() => setStatus('dmxStatus', true, 'All channels cleared'))
        .catch(() => setStatus('dmxStatus', false, 'Failed — is the plugin loaded?'));
}

function sendPreset(val) {
    const ch = parseInt($('#presetChannel').val(), 10);
    if (isNaN(ch) || ch < 1 || ch > 512) { setStatus('presetStatus', false, 'Invalid channel'); return; }
    apiCommand('DMX Bridge - Set Channel', [String(ch), String(val)])
        .then(() => setStatus('presetStatus', true, 'Sent ch ' + ch + ' = ' + val))
        .catch(() => setStatus('presetStatus', false, 'Failed — is the plugin loaded?'));
}
</script>
