    const initialFiles = {
      "index.js": `// مرحباً! ابدأ كتابة الكود هنا 👨‍💻
function hello() {
  console.log('Hello from StacksIDE!');
}
hello();`,
      "index.html": `<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Preview</title>
</head>
<body>
  <h1>Hello from StacksIDE HTML!</h1>
</body>
</html>`,
      "style.css": `/* Your styles here */\nbody{font-family:system-ui}`,
      "README.md": `# Sandbox\n- Explorer\n- Tabs\n- Status bar\n- Run HTML / JS\n\nEnjoy!`
    };

    const languageByExt = (name) => {
      const ext = name.split('.').pop().toLowerCase();
      return ({
        js:'javascript',
        ts:'typescript',
        json:'json',
        md:'markdown',
        css:'css',
        html:'html',
        py:'python',
        java:'java',
        cpp:'cpp',
        c:'c',
        cs:'csharp'
      }[ext]) || 'plaintext';
    };

    let editor, theme = 'vs-dark';
    const models = new Map(); // filename -> file.extension
    let current = null;       // current filename

    const $ = (sel)=>document.querySelector(sel);
    const fileList = $('#fileList');
    const tabs = $('#tabs');
    const cursorEl = $('#cursor');
    const langEl = $('#lang');

    // Modal elements
    const newFileModal = $('#newFileModal');
    const newFileName  = $('#newFileName');
    const newFileExt   = $('#newFileExt');
    const createFileBtn = $('#createFileBtn');
    const cancelFileBtn = $('#cancelFileBtn');

    function openNewFileModal() {
      newFileName.value = "";
      newFileExt.value = "js";
      newFileModal.style.display = "flex";
      newFileName.focus();
    }

    function closeNewFileModal() {
      newFileModal.style.display = "none";
    }

    function createNewFileFromModal() {
      const name = newFileName.value.trim();
      const ext  = newFileExt.value;

      if (!name) {
        alert("اكتب اسم الملف");
        return;
      }

      const fullName = `${name}.${ext}`;

      if (models.has(fullName)) {
        alert("الملف موجود مسبقًا");
        return;
      }

      const lang = languageByExt(fullName);
      const model = monaco.editor.createModel("", lang);

      models.set(fullName, model);
      closeNewFileModal();
      openFile(fullName);
    }

    require.config({
      paths: {
        'vs': 'https://cdnjs.cloudflare.com/ajax/libs/monaco-editor/0.45.0/min/vs'
      }
    });

    require(['vs/editor/editor.main'], () => {

      Object.entries(initialFiles).forEach(([name,content])=>{
        const model = monaco.editor.createModel(content, languageByExt(name));
        models.set(name, model);
      });

      editor = monaco.editor.create(document.getElementById('editor'), {
        model: null,
        theme,
        automaticLayout: true,
        fontSize: 14,
        minimap: { enabled: true },
        wordWrap: 'on',
        tabSize: 2,
        glyphMargin: true,
        smoothScrolling: true,
        renderWhitespace: 'selection',
        contextmenu: true
      });

      editor.onDidChangeCursorPosition((e)=>{
        const p = e.position;
        cursorEl.textContent = `Ln ${p.lineNumber}, Col ${p.column}`;
      });




      refreshList();
      openFile('index.js');
      bindActions();
    });

    function refreshList(){
      fileList.innerHTML = '';
      [...models.keys()].forEach(name=>{
        const li = document.createElement('li');
        li.textContent = name;
        li.className = (name===current)?'active':'';
        li.onclick = ()=>openFile(name);
        fileList.appendChild(li);
      });
      tabs.innerHTML = '';
      [...models.keys()].forEach(name=>{
        const t = document.createElement('div');
        t.className = 'tab'+(name===current?' active':'');
        t.textContent = name;
        t.onclick = ()=>openFile(name);
        tabs.appendChild(t);
      });
    }

    function openFile(name){
      current = name;
      editor.setModel(models.get(name));
      langEl.textContent = models.get(name).getLanguageId();
      refreshList();
    }

    function saveToLocalStorage(){
      const dump = {};
      for(const [k,m] of models){ dump[k] = m.getValue(); }
      localStorage.setItem('monaco-sandbox', JSON.stringify(dump));
      alert('Saved to localStorage');
    }

    function downloadCurrent(){
      if(!current) return;
      const blob = new Blob([models.get(current).getValue()], {type:'text/plain'});
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = current;
      document.body.appendChild(a);
      a.click();
      a.remove();
    }

    function runFile() {
      if (!current) return;




      if (current.endsWith('.html')) {
        const htmlContent = models.get(current).getValue();
        const blob = new Blob([htmlContent], { type: "text/html" });
        const url = URL.createObjectURL(blob);
        window.open(url, "_blank");
        return;
      }

      if (current.endsWith('.js')) {
        try {
          const code = models.get(current).getValue();

          const fn = new Function(code);
          console.clear();
          fn();
        } catch (e) {
          console.error(e);
          alert('See error in devconsole .');
        }
        return;
      }

      alert("This file not supported .\nYou can only run HTML or JS files.");
    }

    function toggleTheme(){
      theme = (theme==='vs-dark') ? 'vs' : 'vs-dark';
      monaco.editor.setTheme(theme);
    }

    function bindActions(){
      $('#newFileBtn').onclick      = openNewFileModal;
      $('#saveBtn').onclick         = saveToLocalStorage;
      $('#downloadBtn').onclick     = downloadCurrent;
      $('#runBtn').onclick          = runFile;
      $('#themeBtn').onclick        = toggleTheme;
      createFileBtn.onclick         = createNewFileFromModal;
      cancelFileBtn.onclick         = closeNewFileModal;

      newFileModal.addEventListener('click', (e)=>{
        if (e.target === newFileModal) closeNewFileModal();
      });
    }