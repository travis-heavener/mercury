$(() => {
    // Load JSON file of all releases
    $.get("releases.json")
        .then(json => handleReleaseJSON(json));
});

const handleReleaseJSON = (releases) => {
    // Iteratively append to DOM
    const urlPrefix = "https://github.com/travis-heavener/mercury/releases/tag/";
    const urlDownloadPrefix = "https://github.com/travis-heavener/mercury/releases/download/";

    const jWindowsTable = $("#downloads-windows-table > tbody");
    const jLinuxTable = $("#downloads-linux-table > tbody");

    // Filter anything but the 10 most recent releases
    releases = releases.slice(0, 10);

    for (const release of releases) {
        // Resolve asset links
        const winSuffix = "Windows_" + release.name.replaceAll(".", "_") + ".zip";
        const linuxSuffix = "Linux_" + release.name.replaceAll(".", "_") + ".tar.gz";
        const downloadWinUrl = urlDownloadPrefix + release.name + "/" + winSuffix;
        const downloadLinuxUrl = urlDownloadPrefix + release.name + "/" + linuxSuffix;

        let winHash = linuxHash = "&ndash;";
        if (release.winHash)
            winHash = release.winHash.substring(0, 6) + "..." + release.winHash.substring(62);
        if (release.linuxHash)
            linuxHash = release.linuxHash.substring(0, 6) + "..." + release.linuxHash.substring(62);

        // Resolve timestamp
        const timeString = new Date(release.date)
            .toLocaleDateString("en-US");

        // Append to DOM
        jWindowsTable.append(`
            <tr>
                <td><a href="${urlPrefix + release.name}">${release.name}</a></td>
                <td>${timeString}</td>
                <td><a href="${downloadWinUrl}">${winSuffix}</a></td>
                <td class="hash-digest" data-full-hash=${release.winHash} title="${release.winHash}">
                    ${winHash}
                    ${release.winHash ? `<img src="clipboard.svg">` : ""}
                </td>
            </tr>
        `);

        jLinuxTable.append(`
            <tr>
                <td><a href="${urlPrefix + release.name}">${release.name}</a></td>
                <td>${timeString}</td>
                <td><a href="${downloadLinuxUrl}">${linuxSuffix}</a></td>
                <td class="hash-digest" data-full-hash=${release.linuxHash} title="${release.linuxHash}">
                    ${linuxHash}
                    ${release.linuxHash ? `<img src="clipboard.svg">` : ""}
                </td>
            </tr>
        `);
    }

    // Bind click events to all hash digests
    $(".hash-digest").on("click", async function() {
        await navigator.clipboard.writeText($(this).attr("data-full-hash"));
        showPassiveModal("Copied to clipboard!");
    });

    // Update the latest release content
    {
        const { name } = releases[0];
        $("#latest-release-div > h3").text(name);

        $("#latest-release-div > a.windows")[0].href = urlDownloadPrefix + name +
            "/Windows_" + name.replaceAll(".", "_") + ".zip";

        $("#latest-release-div > a.linux")[0].href = urlDownloadPrefix + name +
            "/Linux_" + name.replaceAll(".", "_") + ".tar.gz";
    }
};

// Shows a passive modal on screen
const showPassiveModal = (msg) => {
    // Create modal
    const modal = document.createElement("DIV");
    modal.className = "passive-modal";

    // Create text
    const p = document.createElement("P");
    p.innerText = msg
    modal.appendChild(p);

    // Append to DOM
    $(document.body).append(modal);

    // Destroy after 3 seconds
    setTimeout(() => $(modal).remove(), 3000);
};