---
order: 60
description: copyText(text): copies to the clipboard and says whether it worked
---
// Works on https pages and localhost, from a user action such as a click:
// copyButton.addEventListener('click', async () => {
//     copyButton.textContent = (await copyText(code.textContent ?? '')) ? 'Copied' : 'Copy failed';
// });
export async function copyText(text: string): Promise<boolean> {
    try {
        await navigator.clipboard.writeText(text);
        return true;
    } catch {
        return false; // permission denied, insecure page, or no user action
    }
}
