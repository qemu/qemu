document.addEventListener('keydown', (event) => {
    // find a better way to look it up?
    let search_input = document.getElementsByName('q')[0];

    if (event.code === 'KeyS' && document.activeElement !== search_input) {
        event.preventDefault();
        search_input.focus();
    }
});
