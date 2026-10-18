var LudisseyStage = (function() {
    var RULES = LudisseyRules.rules, VIEW = RULES.avatar.view, saved = {}, faces = {};
    var framings = {
        avatar: {yaw: Math.atan2(3, 4), pitch: Math.atan2(0.9, 5), distance: 2.2, target: 0.8},
        figure: {yaw: Math.atan2(1.05, 2.55), pitch: Math.atan2(1.2 - 0.86, Math.hypot(1.05, 2.55)), distance: Math.hypot(1.05, 0.34, 2.55), target: 0.86}
    };
    var vertexSource = 'attribute vec3 p;attribute vec3 n;attribute vec2 t;uniform mat4 m;uniform float k;varying vec3 vn;varying vec2 vt;varying float vy;' +
        'void main(){vn=n;vt=t;vy=p.y;if(k>0.5&&k<1.5){gl_Position=vec4(p.xy,0.9999,1.0);}else{gl_Position=m*vec4(p,1.0);}}';
    var fragmentSource = 'precision mediump float;varying vec3 vn;varying vec2 vt;varying float vy;uniform float k;uniform vec3 c;uniform vec3 top;uniform vec3 low;' +
        'uniform vec3 l;uniform float f;uniform sampler2D s;void main(){if(k>0.5&&k<1.5){gl_FragColor=vec4(mix(low,top,clamp(vy*0.5+0.5,0.0,1.0)),1.0);return;}' +
        'if(k>1.5){float r=length(vt);gl_FragColor=vec4(0.0,0.0,0.0,0.32*(1.0-smoothstep(0.35,1.0,r)));return;}vec3 base=c;' +
        'if(f>0.5&&vt.x>-1.0){vec4 face=texture2D(s,vt);base=mix(base,face.rgb,face.a);}' +
        'float lit=0.5+0.62*max(dot(normalize(vn),l),0.0);gl_FragColor=vec4(base*lit,1.0);}';
    function hex(value) {
        var n = parseInt(String(value || '9AA0A8').replace('#', ''), 16);
        return [(n >> 16 & 255) / 255, (n >> 8 & 255) / 255, (n & 255) / 255];
    }
    function multiply(a, b) {
        var out = new Float32Array(16);
        for (var i = 0; i < 4; i++)
            for (var j = 0; j < 4; j++) out[j * 4 + i] = a[i] * b[j * 4] + a[4 + i] * b[j * 4 + 1] + a[8 + i] * b[j * 4 + 2] + a[12 + i] * b[j * 4 + 3];
        return out;
    }
    function perspective(aspect) {
        var f = 1 / Math.tan(Math.PI / 6), near = 0.1, far = 400;
        return new Float32Array([f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + near) / (near - far), -1, 0, 0, 2 * far * near / (near - far), 0]);
    }
    function lookAt(eye, target) {
        var z = [eye[0] - target[0], eye[1] - target[1], eye[2] - target[2]], zl = Math.hypot(z[0], z[1], z[2]);
        z = [z[0] / zl, z[1] / zl, z[2] / zl];
        var x = [z[2], 0, -z[0]], xl = Math.hypot(x[0], x[2]) || 1;
        x = [x[0] / xl, 0, x[2] / xl];
        var y = [z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]];
        return new Float32Array([x[0], y[0], z[0], 0, x[1], y[1], z[1], 0, x[2], y[2], z[2], 0,
            -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2]), -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2]), -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2]), 1]);
    }
    function program(gl) {
        var made = gl.createProgram();
        [[gl.VERTEX_SHADER, vertexSource], [gl.FRAGMENT_SHADER, fragmentSource]].forEach(function(pair) {
            var shader = gl.createShader(pair[0]);
            gl.shaderSource(shader, pair[1]);
            gl.compileShader(shader);
            gl.attachShader(made, shader);
        });
        gl.linkProgram(made);
        return gl.getProgramParameter(made, gl.LINK_STATUS) ? made : null;
    }
    function buffer(gl, values) {
        var b = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, b);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(values), gl.STATIC_DRAW);
        return b;
    }
    function mesh(gl, vertices, normals, uvs) {
        var count = vertices.length / 3, coords = uvs || [];
        if (!uvs) for (var i = 0; i < count; i++) coords.push(-2, -2);
        return {count: count, p: buffer(gl, vertices), n: buffer(gl, normals), t: buffer(gl, coords)};
    }
    function quad(y, half, depth) {
        var w = half, d = depth || half;
        return [-w, y, -d, w, y, -d, w, y, d, -w, y, -d, w, y, d, -w, y, d];
    }
    function faceImage(gl, url, done) {
        if (faces[url]) return faces[url];
        var entry = {texture: gl.createTexture(), ready: false};
        faces[url] = entry;
        var picture = new Image();
        picture.onload = function() {
            gl.bindTexture(gl.TEXTURE_2D, entry.texture);
            gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
            gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, picture);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
            gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
            entry.ready = true;
            done();
        };
        picture.src = url;
        return entry;
    }
    function clampView(view) {
        view.pitch = Math.min(VIEW.pitchMaximum, Math.max(VIEW.pitchMinimum, view.pitch));
        view.distance = Math.min(VIEW.distanceMaximum, Math.max(VIEW.distanceMinimum, view.distance));
        return view;
    }
    function mount(canvas, look, face, kind) {
        var gl = canvas.getContext('webgl', {antialias: true, alpha: false, preserveDrawingBuffer: true});
        if (!gl || !window.LudisseyCharacters) return false;
        var shader = program(gl);
        if (!shader) return false;
        var key = canvas.dataset.stage, frame = framings[kind] || framings.avatar;
        var view = saved[key] || (saved[key] = clampView({yaw: frame.yaw, pitch: frame.pitch, distance: frame.distance, target: frame.target}));
        var body = LudisseyCharacters[look.bodyPreset === 'female' ? 'female' : 'male'];
        var parts = body.map(function(part) {
            return {part: part.part, face: !!part.face, mesh: mesh(gl, part.vertices, part.normals, part.face)};
        });
        var plate = mesh(gl, quad(0, RULES.baseplate.width / 2), quad(1, 0).map(function(v, i) { return i % 3 === 1 ? 1 : 0; }));
        var shadow = mesh(gl, quad(0.003, 0.55, 0.4), quad(1, 0).map(function() { return 0; }), [-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1]);
        var sky = mesh(gl, [-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, -1, 0, 1, 1, 0, -1, 1, 0], [0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1]);
        var picture = faceImage(gl, face, function() { draw(); });
        var at = {};
        ['p', 'n', 't'].forEach(function(name) { at[name] = gl.getAttribLocation(shader, name); });
        var u = {};
        ['m', 'k', 'c', 'top', 'low', 'l', 'f', 's'].forEach(function(name) { u[name] = gl.getUniformLocation(shader, name); });
        var light = [0.42, 0.78, 0.46], length = Math.hypot(light[0], light[1], light[2]);
        light = light.map(function(v) { return v / length; });
        function bind(m) {
            [['p', 3], ['n', 3], ['t', 2]].forEach(function(pair) {
                gl.bindBuffer(gl.ARRAY_BUFFER, m[pair[0]]);
                gl.enableVertexAttribArray(at[pair[0]]);
                gl.vertexAttribPointer(at[pair[0]], pair[1], gl.FLOAT, false, 0, 0);
            });
        }
        function draw() {
            var scale = Math.min(window.devicePixelRatio || 1, 2), width = Math.max(1, Math.round(canvas.clientWidth * scale)), height = Math.max(1, Math.round(canvas.clientHeight * scale));
            if (canvas.width !== width || canvas.height !== height) {
                canvas.width = width;
                canvas.height = height;
            }
            gl.viewport(0, 0, width, height);
            gl.useProgram(shader);
            gl.disable(gl.DEPTH_TEST);
            gl.uniform1f(u.k, 1);
            gl.uniform3fv(u.top, hex(RULES.lighting.skyTop));
            gl.uniform3fv(u.low, hex(RULES.lighting.skyHorizon));
            bind(sky);
            gl.drawArrays(gl.TRIANGLES, 0, sky.count);
            gl.enable(gl.DEPTH_TEST);
            gl.clear(gl.DEPTH_BUFFER_BIT);
            var eye = [view.distance * Math.cos(view.pitch) * Math.sin(view.yaw), view.target + view.distance * Math.sin(view.pitch), view.distance * Math.cos(view.pitch) * Math.cos(view.yaw)];
            gl.uniformMatrix4fv(u.m, false, multiply(perspective(width / height), lookAt(eye, [0, view.target, 0])));
            gl.uniform3fv(u.l, light);
            gl.uniform1f(u.k, 0);
            gl.uniform1f(u.f, 0);
            gl.uniform3fv(u.c, hex(RULES.part.color));
            bind(plate);
            gl.drawArrays(gl.TRIANGLES, 0, plate.count);
            gl.enable(gl.BLEND);
            gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
            gl.depthMask(false);
            gl.uniform1f(u.k, 2);
            bind(shadow);
            gl.drawArrays(gl.TRIANGLES, 0, shadow.count);
            gl.depthMask(true);
            gl.disable(gl.BLEND);
            gl.uniform1f(u.k, 0);
            gl.activeTexture(gl.TEXTURE0);
            gl.bindTexture(gl.TEXTURE_2D, picture.texture);
            gl.uniform1i(u.s, 0);
            parts.forEach(function(part) {
                gl.uniform3fv(u.c, hex((look.bodyColors || {})[part.part] || RULES.avatar.standard.bodyColors[part.part]));
                gl.uniform1f(u.f, part.face && picture.ready ? 1 : 0);
                bind(part.mesh);
                gl.drawArrays(gl.TRIANGLES, 0, part.mesh.count);
            });
        }
        var pointers = {}, pinch = 0;
        canvas.style.touchAction = 'none';
        canvas.addEventListener('pointerdown', function(event) {
            pointers[event.pointerId] = [event.clientX, event.clientY];
            canvas.setPointerCapture(event.pointerId);
        });
        canvas.addEventListener('pointermove', function(event) {
            var last = pointers[event.pointerId];
            if (!last) return;
            var ids = Object.keys(pointers);
            if (ids.length === 2) {
                pointers[event.pointerId] = [event.clientX, event.clientY];
                var a = pointers[ids[0]], b = pointers[ids[1]], spread = Math.hypot(a[0] - b[0], a[1] - b[1]);
                if (pinch) view.distance *= pinch / spread;
                pinch = spread;
            } else {
                view.yaw -= (event.clientX - last[0]) * 0.01;
                view.pitch += (event.clientY - last[1]) * 0.01;
                pointers[event.pointerId] = [event.clientX, event.clientY];
            }
            clampView(view);
            draw();
        });
        ['pointerup', 'pointercancel', 'lostpointercapture'].forEach(function(name) {
            canvas.addEventListener(name, function(event) {
                delete pointers[event.pointerId];
                pinch = 0;
            });
        });
        canvas.addEventListener('wheel', function(event) {
            event.preventDefault();
            view.distance *= Math.exp(Math.max(-40, Math.min(40, event.deltaY)) * 0.01);
            clampView(view);
            draw();
        }, {passive: false});
        canvas.addEventListener('gesturestart', function(event) { event.preventDefault(); });
        canvas.addEventListener('gesturechange', function(event) { event.preventDefault(); });
        canvas.ludisseyLook = look;
        canvas.ludisseyView = view;
        if (window.ResizeObserver) new ResizeObserver(draw).observe(canvas);
        draw();
        return true;
    }
    return {mount: mount, limits: VIEW, view: function(key) { return saved[key]; }};
})();
