const express = require('express');
const path = require('path');
const bodyParser = require('body-parser');
const fs = require('fs');
const crypto = require('crypto');

const HOST = '127.0.0.1';

function startServer(options) {
  const resourceBaseDir = path.resolve(options.resources);
  const outputRoot = fs.realpathSync(options.output);
  // This executes recursively and synchronously.
  const recursivelyListFiles = (dir) => {
    const absolutePaths = [];
    const files = fs.readdirSync(dir);
    files.forEach((file) => {
      const filepath = path.join(dir, file);
      const stats = fs.statSync(filepath);
      if (stats.isDirectory()) {
        absolutePaths.push(...recursivelyListFiles(filepath));
      } else if (stats.isFile()) {
        absolutePaths.push(path.relative(resourceBaseDir, filepath));
      }
    });
    return absolutePaths;
  };

  const resourceListing = recursivelyListFiles(options.resources);
  console.log('Saw resources', resourceListing);

  const driverHTML = fs.readFileSync(path.join(__dirname, 'run-wasm-gm-tests.html'), 'utf8');
  const testJS = fs.readFileSync(options.js_file, 'utf8');
  const testWASM = fs.readFileSync(options.wasm_file, 'binary');
  const knownHashes = fs.readFileSync(options.known_hashes, 'utf8');

  // Serve the HTML driver and the assets needed to run the tests.
  const app = express();
  app.get('/', (req, res) => res.send(driverHTML));

  app.use('/static/resources/', express.static(resourceBaseDir));
  console.log('resources served from', resourceBaseDir);

  // Receive up to 10MB of image/png data as raw bytes, housed in a buffer.
  app.use(bodyParser.raw({ type: 'image/png', limit: '10mb' }));

  app.get('/static/hashes.txt', (req, res) => res.type('text/plain').send(knownHashes));
  app.get('/static/resource_listing.json', (req, res) => res.json(resourceListing));
  app.get('/static/wasm_gm_tests.js',
    (req, res) => res.type('application/javascript').send(testJS));
  app.get('/static/wasm_gm_tests.wasm', function(req, res) {
    res.type('application/wasm');
    res.send(Buffer.from(testWASM, 'binary'));
  });
  app.post('/write_png', (req, res) => {
    const md5 = req.header('X-MD5-Hash');
    if (!md5 || md5.length !== 32 || !/^[0-9a-fA-F]{32}$/.test(md5)) {
      res.status(400).type('text/plain').send('X-MD5-Hash must be exactly 32 hexadecimal digits.');
      return;
    }
    const newFile = path.join(outputRoot, md5 + '.png');
    const stagingFile = path.join(outputRoot, '.wasm-gm-' + crypto.randomBytes(16).toString('hex'));
    const fd = fs.openSync(stagingFile, 'wx');
    let renamed = false;
    try {
      try {
        fs.writeFileSync(fd, req.body);
      } finally {
        fs.closeSync(fd);
      }
      // Rename replaces the directory entry, not a symlink's target, and permits duplicate digests.
      fs.renameSync(stagingFile, newFile);
      renamed = true;
    } finally {
      if (!renamed) {
        fs.unlinkSync(stagingFile);
      }
    }
    res.sendStatus(200);
  });

  return app.listen(options.port, HOST, () => console.log('- Local web server started.'));
}

module.exports = { startServer, HOST };
