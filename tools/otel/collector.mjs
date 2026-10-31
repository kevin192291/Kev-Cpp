// Kev-C++'s local OpenTelemetry collector: receives OTLP/HTTP JSON trace exports from the app being
// run (SvelteKit's server spans) and passes each one to the IDE as a single stdout line:
//   KEVC_SPANS {"resourceSpans":[...]}
// It listens on 127.0.0.1 only and exits when the IDE closes its stdin.
//   node collector.mjs --port 4318
import http from 'node:http';

const args = process.argv.slice(2);
const port = Number(args[args.indexOf('--port') + 1] || 4318);

const server = http.createServer((req, res) => {
	// Exporters may preflight from a browser; answer CORS so client-side spans can come too.
	res.setHeader('Access-Control-Allow-Origin', '*');
	res.setHeader('Access-Control-Allow-Headers', '*');
	if (req.method === 'OPTIONS') return res.writeHead(204).end();
	if (req.method !== 'POST' || !req.url.startsWith('/v1/traces')) return res.writeHead(404).end();
	const type = req.headers['content-type'] || '';
	if (!type.includes('json')) {
		// protobuf: tell the user how to switch the exporter to JSON.
		process.stdout.write(`KEVC_OTLP_NOTE Got ${type || 'a non-JSON'} export; set OTEL_EXPORTER_OTLP_PROTOCOL=http/json\n`);
		return res.writeHead(415, { 'Content-Type': 'application/json' }).end('{}');
	}
	const chunks = [];
	req.on('data', (c) => chunks.push(c));
	req.on('end', () => {
		try {
			const body = JSON.parse(Buffer.concat(chunks).toString('utf8'));
			process.stdout.write('KEVC_SPANS ' + JSON.stringify(body) + '\n');
			res.writeHead(200, { 'Content-Type': 'application/json' }).end('{}');
		} catch {
			res.writeHead(400).end();
		}
	});
});

server.on('error', (e) => {
	process.stdout.write(`KEVC_OTLP_ERROR ${e.message}\n`);
	process.exit(e.code === 'EADDRINUSE' ? 2 : 1);
});
server.listen(port, '127.0.0.1', () => {
	process.stdout.write(`KEVC_OTLP_READY http://127.0.0.1:${port}\n`);
});
// The IDE holds our stdin open; if it goes away, so do we.
process.stdin.on('end', () => process.exit(0));
process.stdin.resume();
