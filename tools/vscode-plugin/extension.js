/* CodeOne cmap preview.
 *
 * Clicking the preview button on a .bx file opens its pseudo-C rendering
 * (cmap) in a split pane beside it, the way Markdown preview works.
 *
 * The rendering comes from `c1 --cmap`, which writes a .cmap next to its
 * input file. To keep the source tree free of generated files we stage a
 * copy of the .bx in a temp directory and run c1 there.
 */

const vscode = require('vscode');
const cp = require('child_process');
const crypto = require('crypto');
const fs = require('fs');
const os = require('os');
const path = require('path');

/** source .bx fsPath -> the .cmap Uri currently previewing it */
const previews = new Map();

/** Diagnostics sink. Errors surface as a toast; the detail lands here. */
let log;

function activate(context) {
    log = vscode.window.createOutputChannel('CodeOne');
    context.subscriptions.push(log);

    context.subscriptions.push(
        vscode.commands.registerCommand('codeone.previewCmap',
            () => guarded(previewActiveEditor))
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('codeone.showLog', () => {
            log.appendLine(`workspace: ${(vscode.workspace.workspaceFolders || [])
                .map(f => f.uri.fsPath).join(', ') || '(none)'}`);
            log.appendLine(`c1: ${findC1() || '(not found)'}`);
            log.show(true);
        })
    );

    // Refresh the pane when the source is saved, like Markdown preview does.
    context.subscriptions.push(
        vscode.workspace.onDidSaveTextDocument((doc) => {
            if (doc.languageId === 'bx') guarded(() => refreshPreview(doc));
        })
    );

    log.appendLine('CodeOne cmap preview activated.');
}

function deactivate() {
    previews.clear();
}

/* A rejected command shows a toast but loses the stack, which makes
   "I clicked and nothing happened" impossible to diagnose. Log everything
   and reveal the panel when something goes wrong. */
async function guarded(fn) {
    try {
        await fn();
    } catch (err) {
        const msg = err && err.message ? err.message : String(err);
        log.appendLine(`[error] ${msg}`);
        if (err && err.stack) log.appendLine(err.stack);
        log.show(true);
        vscode.window.showErrorMessage(`CodeOne: ${msg}`);
    }
}

function isBxDocument(doc) {
    return doc.languageId === 'bx' || path.extname(doc.uri.fsPath) === '.bx';
}

async function previewActiveEditor() {
    const editor = vscode.window.activeTextEditor;
    if (!editor) {
        vscode.window.showErrorMessage('CodeOne: no active editor.');
        return;
    }
    log.appendLine(`preview requested for ${editor.document.uri.fsPath} ` +
                   `(languageId=${editor.document.languageId})`);
    if (!isBxDocument(editor.document)) {
        vscode.window.showErrorMessage('CodeOne: the active file is not a .bx file.');
        return;
    }
    await showPreview(editor.document);
}

async function showPreview(sourceDoc) {
    const cmapPath = await generateCmap(sourceDoc);
    if (!cmapPath) return;                       // generateCmap already reported

    log.appendLine(`opening ${cmapPath} beside the source`);

    const uri = vscode.Uri.file(cmapPath);
    const previewDoc = await vscode.workspace.openTextDocument(uri);

    // Use cmap's own language: TextMate highlighting, no language server.
    // Hanging the file off `c` made C/C++ extensions flag every int32 as an
    // undefined type; this scope has no diagnostics at all.
    try {
        await vscode.languages.setTextDocumentLanguage(previewDoc, 'cmap');
    } catch (_) {
        /* cosmetic only — never fail the preview over highlighting */
    }

    await vscode.window.showTextDocument(previewDoc, {
        viewColumn: vscode.ViewColumn.Beside,
        preview: true,
        preserveFocus: true
    });

    previews.set(sourceDoc.uri.fsPath, uri);
}

async function refreshPreview(sourceDoc) {
    const previewUri = previews.get(sourceDoc.uri.fsPath);
    if (!previewUri) return;                     // not being previewed

    const cmapPath = await generateCmap(sourceDoc);
    if (!cmapPath) return;

    let text;
    try {
        text = fs.readFileSync(cmapPath, 'utf8');
    } catch (err) {
        vscode.window.showErrorMessage(`CodeOne: cannot read ${cmapPath}: ${err.message}`);
        return;
    }

    // Reopening a file that is already open returns the cached document, so
    // replace its contents instead of relying on a disk re-read.
    try {
        const doc = await vscode.workspace.openTextDocument(previewUri);
        if (doc.getText() === text) return;

        const lastLine = Math.max(0, doc.lineCount - 1);
        const fullRange = new vscode.Range(0, 0, lastLine, doc.lineAt(lastLine).text.length);
        const edit = new vscode.WorkspaceEdit();
        edit.replace(previewUri, fullRange, text);
        await vscode.workspace.applyEdit(edit);
        await doc.save();
    } catch (err) {
        vscode.window.showErrorMessage(`CodeOne: cannot update the preview: ${err.message}`);
    }
}

async function generateCmap(sourceDoc) {
    const c1 = findC1();
    if (!c1) {
        log.appendLine('[error] no c1 found. Looked at:');
        for (const p of c1Candidates()) log.appendLine(`    ${p}`);
        log.show(true);
        vscode.window.showErrorMessage(
            'CodeOne: could not find the c1 compiler. ' +
            'See the "CodeOne" output panel for the paths tried, ' +
            'or set "codeone.c1Path".'
        );
        return null;
    }
    log.appendLine(`using c1: ${c1}`);

    const sourcePath = sourceDoc.uri.fsPath;
    const baseName = path.basename(sourcePath);
    const workDir = path.join(os.tmpdir(), 'codeone-cmap', digest(sourcePath));
    const staged = path.join(workDir, baseName);
    const outPath = path.join(workDir, baseName.replace(/\.bx$/, '') + '.cmap');

    try {
        fs.mkdirSync(workDir, { recursive: true });
        fs.copyFileSync(sourcePath, staged);
        // Drop any earlier output first. A run that was interrupted — or one
        // from an older c1 — leaves a stale file behind, and opening it is
        // what the user would see, not the fresh result.
        fs.rmSync(outPath, { force: true });
    } catch (err) {
        vscode.window.showErrorMessage(`CodeOne: cannot stage the source file: ${err.message}`);
        return null;
    }

    log.appendLine(`running: ${c1} --cmap ${baseName}  (cwd ${workDir})`);
    try {
        await run(c1, ['--cmap', baseName], workDir);
    } catch (err) {
        // c1's parse errors carry file and line, so show them verbatim.
        log.appendLine(`[error] c1 failed: ${err.message}`);
        log.show(true);
        vscode.window.showErrorMessage(`CodeOne: c1 failed on ${baseName}.\n${err.message}`);
        return null;
    }

    if (!fs.existsSync(outPath)) {
        vscode.window.showErrorMessage(`CodeOne: c1 produced no .cmap for ${baseName}.`);
        return null;
    }

    const bytes = fs.statSync(outPath).size;
    log.appendLine(`wrote ${outPath} (${bytes} bytes)`);
    // A runaway renderer once produced a 68 GB file here; opening that wedges
    // the editor with "Invalid string length". Refuse instead of trying.
    if (bytes > 32 * 1024 * 1024) {
        log.show(true);
        vscode.window.showErrorMessage(
            `CodeOne: ${baseName} rendered to ${Math.round(bytes / 1048576)} MB, ` +
            'which is implausible — refusing to open it. This is a c1 bug.'
        );
        return null;
    }
    return outPath;
}

function run(command, args, cwd) {
    return new Promise((resolve, reject) => {
        cp.execFile(command, args, { cwd }, (error, stdout, stderr) => {
            if (error) {
                const detail = (stderr || stdout || error.message).trim();
                reject(new Error(detail));
            } else {
                resolve(stdout);
            }
        });
    });
}

/* Every place findC1 looks, in order. Kept as data so the failure message
   can list exactly what was tried. */
function c1Candidates() {
    const out = [];
    const exe = process.platform === 'win32' ? 'c1.exe' : 'c1';

    const configured = vscode.workspace.getConfiguration('codeone').get('c1Path');
    if (configured) out.push(configured);

    for (const folder of vscode.workspace.workspaceFolders || []) {
        const root = folder.uri.fsPath;
        out.push(path.join(root, 'ssa', 'c1', exe));
        const tcRoot = path.join(root, 'toolchain');
        if (fs.existsSync(tcRoot)) {
            for (const entry of fs.readdirSync(tcRoot))
                out.push(path.join(tcRoot, entry, 'bin', exe));
        }
    }

    // install.sh drops the toolchain here and adds it to the shell's PATH via
    // ~/.codeone/env. VSCode is a GUI app and never sources that file, so
    // check the location directly rather than trusting process.env.PATH.
    out.push(path.join(os.homedir(), '.codeone', 'toolchain', 'bin', exe));

    for (const dir of (process.env.PATH || '').split(path.delimiter))
        if (dir) out.push(path.join(dir, exe));

    return out;
}

function findC1() {
    for (const candidate of c1Candidates()) {
        try {
            fs.accessSync(candidate, fs.constants.X_OK);
            return candidate;
        } catch (_) {
            /* keep looking */
        }
    }
    return null;
}

function digest(s) {
    return crypto.createHash('sha1').update(s).digest('hex').slice(0, 12);
}

module.exports = { activate, deactivate };
