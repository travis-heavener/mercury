$(() => {
    // Load JSON file of all releases
    $.get("releases.json")
        .then(json => handleReleaseJSON(json));
});

const handleReleaseJSON = (releases) => {
    // Iteratively append to DOM
    const urlPrefix = "https://github.com/travis-heavener/mercury/releases/tag/";
    const downloadPrefix = "https://github.com/travis-heavener/mercury/releases/download/";

    const jWindowsTable = $("#downloads-windows-table > tbody");
    const jLinuxTable = $("#downloads-linux-table > tbody");

    // Filter anything but the 10 most recent releases
    releases = releases.slice(0, 10);

    // Row maker helper function
    const genRow = (name, timeString, downloadUrl, suffix, shortHash, fullHash) => `
            <tr>
                <td><a href="${urlPrefix + name}">${name}</a></td>
                <td>${timeString}</td>
                <td><a href="${downloadUrl}">${suffix}</a></td>
                <td class="hash-digest" data-full-hash=${fullHash} title="Copy to clipboard">
                    ${shortHash}
                    ${fullHash ? `<img src="clipboard.svg">` : ""}
                </td>
            </tr>
        `;

    // Generate release rows
    for (const { name, date, winHash, linuxHash } of releases) {
        // Resolve asset links
        const winSuffix = "Windows_" + name.replaceAll(".", "_") + ".zip";
        const linuxSuffix = "Linux_" + name.replaceAll(".", "_") + ".tar.gz";
        const winUrl = downloadPrefix + name + "/" + winSuffix;
        const linuxUrl = downloadPrefix + name + "/" + linuxSuffix;

        const winShortHash = !winHash ? "&ndash;" : (winHash.substring(0, 6) + "..." + winHash.substring(62));
        const linuxShortHash = !linuxHash ? "&ndash;" : (linuxHash.substring(0, 6) + "..." + linuxHash.substring(62));

        // Resolve timestamp
        const timeString = new Date(date).toLocaleDateString("en-US");

        // Append to DOM
        jWindowsTable.append(genRow(name, timeString, winUrl, winSuffix, winShortHash, winHash));
        jLinuxTable.append(genRow(name, timeString, linuxUrl, linuxSuffix, linuxShortHash, linuxHash));
    }

    // Bind click events to all hash digests
    $(".hash-digest").on("click", async function() {
        await navigator.clipboard.writeText($(this).attr("data-full-hash"));
        showToast("Copied to clipboard!");
    });

    // Update the latest release content
    {
        const { name } = releases[0];
        $("#latest-release-div > h3").text(name);

        $("#latest-release-div > a.windows")[0].href = downloadPrefix + name +
            "/Windows_" + name.replaceAll(".", "_") + ".zip";

        $("#latest-release-div > a.linux")[0].href = downloadPrefix + name +
            "/Linux_" + name.replaceAll(".", "_") + ".tar.gz";
    }
};

// Shows a toast on screen
const showToast = (msg) => {
    // Create modal
    const modal = document.createElement("DIV");
    modal.className = "toast";

    // Create text
    const p = document.createElement("P");
    p.innerText = msg
    modal.appendChild(p);

    // Add progress bar
    const progress = document.createElement("DIV");
    progress.className = "progress-bar";
    modal.appendChild(progress);

    document.body.appendChild(modal);

    // Start fade out before removal
    setTimeout(() => {
        modal.classList.add("fade-out");
        setTimeout(() => modal.remove(), 400); // matches fade duration
    }, 3000);

};