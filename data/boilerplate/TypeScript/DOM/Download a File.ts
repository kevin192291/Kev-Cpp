---
order: 70
description: downloadFile(name, data, type): saves text or binary data made in the page as a file
---
// downloadFile('report.csv', csvText, 'text/csv');
// downloadFile('data.json', JSON.stringify(data, null, 4), 'application/json');
export function downloadFile(filename: string, data: BlobPart, type = 'application/octet-stream'): void {
    const url = URL.createObjectURL(new Blob([data], { type }));
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    link.click();
    // Revoke a moment later; revoking at once can cancel the download in some browsers.
    setTimeout(() => URL.revokeObjectURL(url), 1000);
}
