const App = (() => {
    const navItems = [
        ['dashboard', '/', 'Dashboard'],
        ['ble', '/ble.html', 'BLE Devices'],
        ['settings', '/settings.html', 'Settings'],
        ['narodmon', '/narodmon.html', 'Narodmon Payload']
    ];

    function initHeader() {
        const headerHost = document.querySelector('[data-app-header]');
        if (!headerHost) {
            return;
        }

        const page = document.body.dataset.page || '';
        const title = headerHost.dataset.title || 'BLEBridge';
        const links = navItems
            .map(([id, href, label]) => `<a href="${href}" class="${id === page ? 'active' : ''}">${label}</a>`)
            .join('');

        headerHost.innerHTML = `<header class="app-header"><h1 id="page-title">${title}</h1><nav class="app-nav">${links}</nav></header>`;
    }

    function escapeHtml(value) {
        return String(value || '')
            .replaceAll('&', '&amp;')
            .replaceAll('<', '&lt;')
            .replaceAll('>', '&gt;')
            .replaceAll('"', '&quot;')
            .replaceAll("'", '&#39;');
    }

    async function refreshStatus() {
        const statusBody = document.getElementById('status-body');
        const pageTitle = document.getElementById('page-title');

        if (!statusBody) {
            return;
        }

        try {
            const response = await fetch('/status.json', { cache: 'no-store' });
            const data = await response.json();

            if (pageTitle && document.body.dataset.page === 'dashboard') {
                pageTitle.textContent = `BLEBridge ${data.narodmon_device_name}`;
            }

            const flashFreePercent = data.flash_total_bytes > 0
                ? ((data.flash_free_bytes * 100) / data.flash_total_bytes).toFixed(1)
                : '0.0';
            const rows = [
                ['Flash Free', `${flashFreePercent}% (${(data.flash_free_bytes / 1024).toFixed(1)} KB)`],
                ['CPU Load', `${data.cpu_load_percent}%`],
                ['Narodmon Send In', `${data.narodmon_seconds_remaining}s`],
                ['Device Table', `${((data.device_table_used * 100) / data.device_table_capacity).toFixed(1)}%`],
                ['Measurement Buffer', `${((data.measurement_table_used * 100) / data.measurement_table_capacity).toFixed(1)}%`],
                ['Aggregate View', `${((data.measurement_view_used * 100) / data.measurement_view_capacity).toFixed(1)}%`]
            ];

            statusBody.innerHTML = rows
                .map(([name, value]) => `<tr><th>${name}</th><td>${value}</td></tr>`)
                .join('');
        } catch (error) {
            statusBody.innerHTML = '<tr><td class="muted">Failed to load resource counters</td></tr>';
        }
    }

    return {
        initHeader,
        refreshStatus,
        escapeHtml
    };
})();
