# Frontier

## Browser world-editor mirror

This branch stages the standalone **Frontier World Mirror** HTML prototype from
`streamlinkinbox/Frontier` (`arena/587b292c-frontier`) for review. It is an
interactive browser-side rendering/editor mirror; it is not the native C++
editor executable.

Run it from the repository root with:

```bash
python3 Experimental/FrontierWorldMirror/Serve.py 8098
```

The page is served at `/` (or `/Experimental/FrontierWorldMirror/index.html`).
`Experimental/ProjectBrowser/` is included for the mirror's Projects link.
