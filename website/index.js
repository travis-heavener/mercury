/******************************************************************************
 * MERCURY RELEASE PORTAL SCRIPT
 ******************************************************************************/

const urlPrefix = "https://github.com/travis-heavener/mercury/releases/tag/";
const downloadPrefix = "https://github.com/travis-heavener/mercury/releases/download/";
const MAX_DISPLAYED_RELEASES = 5;

// Used to cache agent markdown content
let cachedAgentText = "";

// Initialize DOM bindings on document ready
document.addEventListener("DOMContentLoaded", () => {
    // Fetch release catalogue
    fetch("releases.json")
        .then((resp) => resp.json())
        .then((json) => handleReleaseJSON(json))
        .catch(() => {
            // Silently fallback if releases cannot be loaded
        });

    // Bind distro selector change event
    const pDistroSelect = document.getElementById("distro-select");
    if (pDistroSelect) {
        pDistroSelect.addEventListener("change", updateDistroView);
        updateDistroView();
    }

    // Bind navigation anchors to switch distro delivery mode
    bindNavAnchors();

    // Bind agent markdown copy buttons
    const pAgentButtons = document.querySelectorAll("#copy-agent-btn, #hero-copy-agent-btn");
    for (let i = 0; i < pAgentButtons.length; ++i) {
        pAgentButtons[i].addEventListener("click", handleAgentCopyClick);
    }

    // Handle initial URL hash navigation on load
    handleHashNavigation();
    window.addEventListener("hashchange", handleHashNavigation);

    // Update current copyright year
    const pYearElem = document.getElementById("current-year");
    if (pYearElem) pYearElem.textContent = (new Date()).getFullYear().toString();
});

// Used to switch Linux delivery mode and optionally scroll to target
const setDistroMode = (mode, targetId) => {
    const pDistroSelect = document.getElementById("distro-select");
    if (!pDistroSelect) return;

    pDistroSelect.value = mode;
    updateDistroView();

    if (targetId) {
        const pTarget = document.getElementById(targetId);
        if (pTarget) pTarget.scrollIntoView({ behavior: "smooth" });
    }
};

// Used to bind click events on navigation anchors
const bindNavAnchors = () => {
    const pBuildAnchors = document.querySelectorAll("a[href='#build-instructions']");
    for (let i = 0; i < pBuildAnchors.length; ++i) {
        pBuildAnchors[i].addEventListener("click", (event) => {
            event.preventDefault();
            setDistroMode("diy", "build-instructions");
            history.pushState(null, "", "#build-instructions");
        });
    }

    const pLinuxAnchors = document.querySelectorAll("a[href='#linux-download-table']");
    for (let i = 0; i < pLinuxAnchors.length; ++i) {
        pLinuxAnchors[i].addEventListener("click", (event) => {
            event.preventDefault();
            setDistroMode("download", "linux-download-table");
            history.pushState(null, "", "#linux-download-table");
        });
    }
};

// Used to handle URL hash navigation state
const handleHashNavigation = () => {
    const hash = window.location.hash;
    if (hash === "#build-instructions") {
        setDistroMode("diy", "build-instructions");
    } else if (hash === "#linux-download-table") {
        setDistroMode("download", "linux-download-table");
    }
};

// Used to toggle Linux downloads table vs build instructions
const updateDistroView = () => {
    const pDistroSelect = document.getElementById("distro-select");
    const pLinuxTable = document.getElementById("downloads-linux-table");
    const pBuildInstructions = document.getElementById("build-instructions");
    if (!pDistroSelect || !pLinuxTable || !pBuildInstructions) return;

    const val = (pDistroSelect.value || "").toString().trim();
    const pScrollContainer = pLinuxTable.closest(".table-scroll-container") || pLinuxTable;

    if (val === "download") {
        pScrollContainer.style.display = "";
        pBuildInstructions.style.display = "none";
    } else {
        pScrollContainer.style.display = "none";
        pBuildInstructions.style.display = "block";
    }
};

// Used to resolve Linux archive asset prefix based on release version
// (Releases v0.28.4 - v0.29.2 use Debian_X.X.X.tar.gz instead of Linux_X.X.X.tar.gz)
const getLinuxPrefix = (name) => {
    const parts = name.substring(1).split(".");
    const major = parseInt(parts[0], 10);
    const minor = parseInt(parts[1], 10);
    const patch = parseInt(parts[2], 10);

    return (major === 0 &&
        (
            (minor === 28 && patch >= 4) ||
            (minor === 29 && patch <= 2)
        )) ? "Debian" : "Linux";
};

// Used to handle copy Markdown for AI agents
const handleAgentCopyClick = async (event) => {
    const pButton = event.currentTarget;
    if (!navigator.clipboard || !navigator.clipboard.writeText) {
        showToast("Clipboard access unavailable");
        return;
    }

    try {
        if (!cachedAgentText) {
            const resp = await fetch("llms.txt");
            if (!resp.ok) throw new Error("Failed to load llms.txt");
            cachedAgentText = await resp.text();
        }

        await navigator.clipboard.writeText(cachedAgentText);
        showToast("Copied Markdown for agents");

        // Provide temporary visual confirmation
        const pSpan = pButton.querySelector("span");
        if (pSpan) {
            const originalText = pSpan.textContent;
            pSpan.textContent = "Copied!";
            setTimeout(() => {
                pSpan.textContent = originalText;
            }, 2000);
        }
    } catch {
        showToast("Failed to copy Markdown");
    }
};

// Used to copy SHA-256 hash digest to system clipboard
const handleHashClick = async (event) => {
    const pTarget = event.currentTarget;
    const fullHash = pTarget.getAttribute("data-full-hash");
    if (!fullHash) return;

    if (!navigator.clipboard || !navigator.clipboard.writeText) {
        showToast("Clipboard access unavailable");
        return;
    }

    try {
        await navigator.clipboard.writeText(fullHash);
        showToast("SHA-256 copied to clipboard");
    } catch {
        showToast("Failed to copy hash");
    }
};

// Used to generate release table row element
const genRow = (name, timeString, downloadUrl, suffix, shortHash, fullHash) => {
    const pRow = document.createElement("tr");

    pRow.innerHTML = `
        <td data-label="Version"><a class="version-link" href="${urlPrefix + name}">${name}</a></td>
        <td data-label="Released"><span class="date-badge">${timeString}</span></td>
        <td data-label="Download"><a class="asset-link" href="${downloadUrl}"><code>${suffix}</code></a></td>
        <td data-label="SHA-256" class="hash-digest" data-full-hash="${fullHash}" title="Copy SHA-256 hash to clipboard">
            <span class="hash-text">${shortHash}</span>
            ${fullHash ? `<img class="copy-icon" src="clipboard.svg" alt="Copy to clipboard">` : ""}
        </td>
    `;

    return pRow;
};

// Used to populate release download tables from JSON array
const handleReleaseJSON = (releases) => {
    const pWinTbody = document.querySelector("#downloads-windows-table > tbody");
    const pLinuxTbody = document.querySelector("#downloads-linux-table > tbody");
    if (!pWinTbody || !pLinuxTbody || !releases || releases.length === 0) return;

    // Filter to most recent releases
    const recent = releases.slice(0, MAX_DISPLAYED_RELEASES);

    // Populate rows
    for (let i = 0; i < recent.length; ++i) {
        const item = recent[i];
        const name = item.name;
        const date = item.date;
        const winHash = item.winHash || "";
        const linuxHash = item.linuxHash || "";

        // Resolve asset filenames and download URLs
        const winSuffix = "Windows_" + name.replaceAll(".", "_") + ".zip";
        const linuxSuffix = `${getLinuxPrefix(name)}_` + name.replaceAll(".", "_") + ".tar.gz";
        const winUrl = downloadPrefix + name + "/" + winSuffix;
        const linuxUrl = downloadPrefix + name + "/" + linuxSuffix;

        // Truncate hash representations
        const winShortHash = !winHash ? "–" : (winHash.substring(0, 6) + "..." + winHash.substring(62));
        const linuxShortHash = !linuxHash ? "–" : (linuxHash.substring(0, 6) + "..." + linuxHash.substring(62));

        // Format locale timestamp
        const timeString = new Date(date).toLocaleDateString("en-US");

        // Append generated elements to respective tables
        const pWinRow = genRow(name, timeString, winUrl, winSuffix, winShortHash, winHash);
        const pLinuxRow = genRow(name, timeString, linuxUrl, linuxSuffix, linuxShortHash, linuxHash);

        pWinTbody.appendChild(pWinRow);
        pLinuxTbody.appendChild(pLinuxRow);
    }

    // Attach click listeners to all hash digest elements
    const pHashElements = document.querySelectorAll(".hash-digest");
    for (let i = 0; i < pHashElements.length; ++i) {
        pHashElements[i].addEventListener("click", handleHashClick);
    }

    // Update hero release metadata
    const latest = releases[0];
    if (latest) {
        const name = latest.name;
        const pLatestTag = document.querySelector("#latest-release-div h3");
        if (pLatestTag) pLatestTag.textContent = name;

        const pWinBtn = document.querySelector("#latest-release-div a.windows");
        if (pWinBtn) {
            pWinBtn.href = downloadPrefix + name + "/Windows_" + name.replaceAll(".", "_") + ".zip";
        }

        const pLinuxBtn = document.querySelector("#latest-release-div a.linux");
        if (pLinuxBtn) {
            pLinuxBtn.href = downloadPrefix + name + `/${getLinuxPrefix(name)}_` + name.replaceAll(".", "_") + ".tar.gz";
        }
    }
};

// Used to display transient feedback toast
const showToast = (msg) => {
    // Remove existing toasts
    const pExisting = document.querySelectorAll(".toast");
    for (let i = 0; i < pExisting.length; ++i) {
        pExisting[i].remove();
    }

    // Construct toast container
    const pModal = document.createElement("div");
    pModal.className = "toast";

    // Append message label
    const pText = document.createElement("p");
    pText.textContent = msg;
    pModal.appendChild(pText);

    // Append animated timer bar
    const pProgress = document.createElement("div");
    pProgress.className = "progress-bar";
    pModal.appendChild(pProgress);

    document.body.appendChild(pModal);

    // Schedule fade out and removal
    setTimeout(() => {
        pModal.classList.add("fade-out");
        setTimeout(() => pModal.remove(), 400);
    }, 2600);
};
