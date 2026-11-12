---
order: 110
section: helpers
description: Asks before leaving a page with unsaved changes (links, back, closing the tab); in <script lang="ts">
---
import { beforeNavigate } from '$app/navigation';

// Set dirty = true when the form changes and back to false after saving.
let dirty = $state(false);

beforeNavigate((navigation) => {
    if (!dirty) {
        return;
    }
    if (navigation.type === 'leave') {
        // Closing or reloading the tab: cancelling makes the browser show its own "Leave site?" prompt.
        navigation.cancel();
    } else if (!confirm('You have unsaved changes. Leave this page?')) {
        navigation.cancel();
    }
});
