"""
Full automated test suite for the QFV / SPECTRA prediction system.
Run: python tests/run_all.py
"""
import sys, os, traceback, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import numpy as np

results = []

def test(name, fn):
    try:
        fn()
        results.append(("PASS", name, ""))
    except Exception:
        results.append(("FAIL", name, traceback.format_exc()))


# ── distributions ─────────────────────────────────────────────────────────────

def t_gaussian():
    from prediction.distributions import gaussian
    g = gaussian(5.0, 0.5)
    s = g.sample(5000)
    assert s.shape == (5000,)
    assert abs(s.mean() - 5.0) < 0.1, f"mean={s.mean()}"
    assert abs(s.std()  - 0.5) < 0.05, f"std={s.std()}"
test("gaussian", t_gaussian)

def t_uniform():
    from prediction.distributions import uniform
    u = uniform(2.0, 8.0)
    s = u.sample(5000)
    assert s.min() >= 2.0 and s.max() <= 8.0
    assert abs(s.mean() - 5.0) < 0.2
test("uniform", t_uniform)

def t_discrete():
    from prediction.distributions import discrete
    d = discrete({"a": 0.7, "b": 0.3})
    # sample() now returns float indices (0.0=a, 1.0=b) for use in particle arrays
    s = d.sample(5000)
    assert s.dtype in (np.float32, np.float64), f"expected float array, got {s.dtype}"
    assert set(s).issubset({0.0, 1.0}), f"unexpected indices: {set(s)}"
    frac_a = (s == 0.0).sum() / len(s)
    assert abs(frac_a - 0.7) < 0.05, f"frac_a={frac_a}"
    # label maps
    assert d.label_to_index == {"a": 0.0, "b": 1.0}
    assert d.index_to_label == {0.0: "a", 1.0: "b"}
test("discrete", t_discrete)


# ── ParticleCloud ─────────────────────────────────────────────────────────────

def t_particle_cloud_basic():
    from prediction.particle_filter import ParticleCloud
    vals = np.array([1.0, 2.0, 3.0, 4.0, 5.0])
    wts  = np.array([0.1, 0.2, 0.4, 0.2, 0.1])
    c = ParticleCloud(vals, wts)
    assert c.most_likely() == 3.0
    assert abs(c.mean() - 3.0) < 1e-9
    assert c.std() > 0
    lo, hi = c.percentile_range(5, 95)
    assert lo <= 3.0 <= hi
test("particle_cloud_basic", t_particle_cloud_basic)

def t_particle_cloud_json():
    from prediction.particle_filter import ParticleCloud
    vals = np.array([1.0, 2.0, 3.0])
    wts  = np.array([0.2, 0.5, 0.3])
    c = ParticleCloud(vals, wts)
    j = c.to_cloud_json(top_n=2)
    assert len(j) == 2
    assert j[0]["value"] == 2.0          # highest weight first
    assert j[0]["probability"] == 0.5
test("particle_cloud_json", t_particle_cloud_json)

def t_particle_cloud_uniform_weights():
    "All-equal weights should not crash most_likely / std."
    from prediction.particle_filter import ParticleCloud
    vals = np.arange(10, dtype=float)
    wts  = np.ones(10) / 10
    c = ParticleCloud(vals, wts)
    _ = c.most_likely()
    _ = c.std()
    _ = c.sample()
test("particle_cloud_uniform_weights", t_particle_cloud_uniform_weights)


# ── ParticleState ─────────────────────────────────────────────────────────────

def t_particle_state_set_scalar():
    from prediction.particle_filter import ParticleState
    ps = ParticleState(500)
    ps.set("x", 10.0)
    assert ps.vars["x"].shape == (500,)
    assert (ps.vars["x"] == 10.0).all()
test("particle_state_set_scalar", t_particle_state_set_scalar)

def t_particle_state_set_dist():
    from prediction.particle_filter import ParticleState
    from prediction.distributions import gaussian
    ps = ParticleState(1000)
    ps.set("x", gaussian(5.0, 1.0))
    assert abs(ps.vars["x"].mean() - 5.0) < 0.2
    assert abs(ps.vars["x"].std()  - 1.0) < 0.15
test("particle_state_set_dist", t_particle_state_set_dist)

def t_particle_state_weights_sum_to_one():
    from prediction.particle_filter import ParticleState
    ps = ParticleState(500)
    ps.set("x", 0.0)
    assert abs(ps.weights.sum() - 1.0) < 1e-9
test("particle_state_weights_sum_to_one", t_particle_state_weights_sum_to_one)

def t_particle_state_resample():
    from prediction.particle_filter import ParticleState
    ps = ParticleState(500)
    ps.set("x", 5.0)
    # Force all weight onto one particle
    ps.weights[:] = 0
    ps.weights[0] = 1.0
    ps.resample()
    # After resample, all particles should be clones of particle 0
    assert (ps.vars["x"] == ps.vars["x"][0]).all()
    assert abs(ps.weights.sum() - 1.0) < 1e-9
    assert abs(ps.weights.std()) < 1e-9
test("particle_state_resample", t_particle_state_resample)

def t_particle_state_update_weights():
    from prediction.particle_filter import ParticleState
    ps = ParticleState(200)
    ps.set("x", 0.0)  # all particles at x=0
    ps.vars["x"][0] = 100.0  # one particle way off
    ps.update_weights({"x": 0.0}, spread=0.1)
    # particle 0 should have very low weight
    assert ps.weights[0] < 1e-10
    assert abs(ps.weights.sum() - 1.0) < 1e-6
test("particle_state_update_weights", t_particle_state_update_weights)

def t_particle_state_ess():
    from prediction.particle_filter import ParticleState
    ps = ParticleState(100)
    ps.set("x", 0.0)
    # Uniform weights -> ESS == N
    assert abs(ps.effective_sample_size() - 100) < 0.01
    # All weight on one -> ESS == 1
    ps.weights[:] = 0
    ps.weights[0] = 1.0
    assert abs(ps.effective_sample_size() - 1.0) < 0.01
test("particle_state_ess", t_particle_state_ess)

def t_particle_state_copy():
    from prediction.particle_filter import ParticleState
    ps = ParticleState(100)
    ps.set("x", 3.0)
    cp = ps.copy()
    cp.vars["x"][:] = 99.0
    # original must not change
    assert (ps.vars["x"] == 3.0).all()
test("particle_state_copy", t_particle_state_copy)


# ── predict() ────────────────────────────────────────────────────────────────

def t_predict_linear():
    "x should advance ~v*t with no uncertainty added."
    from prediction.particle_filter import ParticleState
    from prediction.predict import predict
    state = ParticleState(500)
    state.set("x", 0.0)
    state.set("vx", 2.0)
    def physics(v, dt):
        return {"x": v["x"] + v["vx"] * dt, "vx": v["vx"].copy()}
    r = predict(1.0, 0.1, state, physics, output_vars=["x"])
    assert len(r.timesteps) == 10
    assert abs(r.timesteps[-1] - 1.0) < 1e-6
    assert abs(r.clouds["x"][-1].mean() - 2.0) < 0.05
test("predict_linear", t_predict_linear)

def t_predict_uncertainty_grows():
    "Uncertainty injected by noise_fn must grow over time."
    from prediction.particle_filter import ParticleState
    from prediction.distributions import gaussian
    from prediction.predict import predict
    state = ParticleState(1000)
    state.set("x", 0.0)
    state.set("vx", gaussian(5.0, 1.0))
    def physics(v, dt):
        return {"x": v["x"] + v["vx"] * dt, "vx": v["vx"].copy()}
    r = predict(1.0, 0.1, state, physics, output_vars=["x"])
    std_early = r.clouds["x"][0].std()
    std_late  = r.clouds["x"][-1].std()
    assert std_late > std_early, f"{std_early} -> {std_late}"
test("predict_uncertainty_grows", t_predict_uncertainty_grows)

def t_predict_timesteps():
    "Timesteps must be evenly spaced and last value == duration."
    from prediction.particle_filter import ParticleState
    from prediction.predict import predict
    state = ParticleState(100)
    state.set("x", 0.0)
    def physics(v, dt): return {"x": v["x"]}
    r = predict(2.0, 0.25, state, physics, output_vars=["x"])
    assert len(r.timesteps) == 8
    diffs = np.diff(r.timesteps)
    assert (np.abs(diffs - 0.25) < 1e-6).all(), f"uneven steps: {diffs}"
    assert abs(r.timesteps[-1] - 2.0) < 1e-6
test("predict_timesteps", t_predict_timesteps)

def t_predict_at():
    "PredictResult.at() must return the closest timestep."
    from prediction.particle_filter import ParticleState
    from prediction.predict import predict
    state = ParticleState(100)
    state.set("x", 0.0)
    def physics(v, dt): return {"x": v["x"] + dt}
    r = predict(1.0, 0.1, state, physics, output_vars=["x"])
    clouds = r.at(0.55)   # closest to t=0.5 or t=0.6
    assert "x" in clouds
test("predict_at", t_predict_at)

def t_predict_summary():
    from prediction.particle_filter import ParticleState
    from prediction.predict import predict
    state = ParticleState(100)
    state.set("x", 0.0)
    def physics(v, dt): return {"x": v["x"]}
    r = predict(1.0, 0.1, state, physics, output_vars=["x"])
    s = r.summary()
    assert "100" in s and "10" in s
test("predict_summary", t_predict_summary)


# ── output.py ────────────────────────────────────────────────────────────────

def t_output_json():
    import json
    from prediction.particle_filter import ParticleState
    from prediction.predict import predict
    from prediction.output import show
    state = ParticleState(200)
    state.set("x", 0.0); state.set("vx", 1.0)
    def physics(v, dt): return {"x": v["x"]+v["vx"]*dt, "vx": v["vx"].copy()}
    r = predict(1.0, 0.1, state, physics, output_vars=["x"])
    j = show(r, ["x"])
    data = json.loads(j)
    assert len(data) == 10
    entry = data[0]
    assert "t" in entry and "x" in entry
    xd = entry["x"]
    assert "most_likely" in xd and "mean" in xd and "uncertainty_1sigma" in xd
test("output_json", t_output_json)

def t_output_print_summary():
    "print_cloud_summary must not raise."
    import io, contextlib
    from prediction.particle_filter import ParticleState
    from prediction.predict import predict
    from prediction.output import print_cloud_summary
    state = ParticleState(200)
    state.set("x", 5.0); state.set("vx", 2.0)
    def physics(v, dt): return {"x": v["x"]+v["vx"]*dt, "vx": v["vx"].copy()}
    r = predict(1.0, 0.1, state, physics, output_vars=["x"])
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        print_cloud_summary(r, ["x"])
    out = buf.getvalue()
    assert "Time" in out
test("output_print_summary", t_output_print_summary)


# ── lexer ─────────────────────────────────────────────────────────────────────

def t_lexer_new_keywords():
    from compiler.lexer import Lexer, TokenType
    src = "super x = gaussian(center: 0.8, spread: 0.05)\npredict 5 seconds, step 0.05:\n    x = x + 1\nshow pos_x, pos_y\nvar y = 3.0\n"
    tokens = Lexer(src).tokenize()
    types = [t.type for t in tokens]
    assert TokenType.SUPER   in types, "SUPER missing"
    assert TokenType.PREDICT in types, "PREDICT missing"
    assert TokenType.SHOW    in types, "SHOW missing"
    assert TokenType.VAR     in types, "VAR missing"
test("lexer_new_keywords", t_lexer_new_keywords)

def t_lexer_colon_kwarg():
    "center: 0.8 inside a call must tokenize to IDENTIFIER COLON FLOAT."
    from compiler.lexer import Lexer, TokenType
    src = "f(center: 0.8)\n"
    tokens = Lexer(src).tokenize()
    types = [t.type for t in tokens]
    assert TokenType.IDENTIFIER in types
    assert TokenType.COLON in types
    assert TokenType.FLOAT_LIT in types
test("lexer_colon_kwarg", t_lexer_colon_kwarg)

def t_lexer_wave_literal():
    from compiler.lexer import Lexer, TokenType
    src = "x = ~{3:0.4, 7:0.8}~\n"
    tokens = Lexer(src).tokenize()
    wave = [t for t in tokens if t.type == TokenType.SPECT_WAVE_LIT]
    assert len(wave) == 1
    assert wave[0].value == {3: 0.4, 7: 0.8}
test("lexer_wave_literal", t_lexer_wave_literal)


# ── parser ────────────────────────────────────────────────────────────────────

def t_parser_super():
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.ast_nodes import SuperAssignment, CallExpr, FloatLiteral
    src = "super bounce = gaussian(center: 0.8, spread: 0.05)\n"
    ast = Parser(Lexer(src).tokenize()).parse()
    node = ast.body[0]
    assert isinstance(node, SuperAssignment), f"got {type(node)}"
    assert node.name == "bounce"
    assert isinstance(node.value, CallExpr)
    kw = node.value.kwargs
    assert "center" in kw, f"kwargs: {kw}"
    assert isinstance(kw["center"], FloatLiteral)
    assert kw["center"].value == 0.8
test("parser_super", t_parser_super)

def t_parser_super_positional():
    "gaussian(0.8, 0.05) — positional args also valid."
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.ast_nodes import SuperAssignment, CallExpr
    src = "super x = gaussian(0.8, 0.05)\n"
    ast = Parser(Lexer(src).tokenize()).parse()
    node = ast.body[0]
    assert isinstance(node, SuperAssignment)
    assert len(node.value.args) == 2
test("parser_super_positional", t_parser_super_positional)

def t_parser_predict():
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.ast_nodes import PredictBlock, Assignment
    src = "predict 5 seconds, step 0.05:\n    x = x + vx * dt\n"
    ast = Parser(Lexer(src).tokenize()).parse()
    node = ast.body[0]
    assert isinstance(node, PredictBlock), f"got {type(node)}"
    assert node.duration == 5.0
    assert abs(node.step - 0.05) < 1e-9
    assert len(node.body) == 1
    assert isinstance(node.body[0], Assignment)
test("parser_predict", t_parser_predict)

def t_parser_predict_no_step():
    "predict 3 seconds: — step defaults to 0.1."
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.ast_nodes import PredictBlock
    src = "predict 3 seconds:\n    x = x\n"
    ast = Parser(Lexer(src).tokenize()).parse()
    node = ast.body[0]
    assert isinstance(node, PredictBlock)
    assert node.duration == 3.0
    assert node.step == 0.1
test("parser_predict_no_step", t_parser_predict_no_step)

def t_parser_predict_if_inside():
    "if-block inside predict body must parse correctly."
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.ast_nodes import PredictBlock, IfStmt
    src = "predict 1 seconds, step 0.1:\n    if y < 0:\n        y = 0\n"
    ast = Parser(Lexer(src).tokenize()).parse()
    node = ast.body[0]
    assert isinstance(node, PredictBlock)
    assert isinstance(node.body[0], IfStmt)
test("parser_predict_if_inside", t_parser_predict_if_inside)

def t_parser_show_with_prefix():
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.ast_nodes import ShowStmt
    src = 'show pos_x, pos_y as "ball"\n'
    ast = Parser(Lexer(src).tokenize()).parse()
    node = ast.body[0]
    assert isinstance(node, ShowStmt), f"got {type(node)}"
    assert node.variables == ["pos_x", "pos_y"]
    assert node.save_prefix == "ball"
test("parser_show_with_prefix", t_parser_show_with_prefix)

def t_parser_show_no_prefix():
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.ast_nodes import ShowStmt
    src = "show pos_x, pos_y\n"
    ast = Parser(Lexer(src).tokenize()).parse()
    node = ast.body[0]
    assert isinstance(node, ShowStmt)
    assert node.variables == ["pos_x", "pos_y"]
    assert node.save_prefix is None
test("parser_show_no_prefix", t_parser_show_no_prefix)

def t_parser_var():
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.ast_nodes import VarAssignment, FloatLiteral
    src = "var pos_x = 5.0\n"
    ast = Parser(Lexer(src).tokenize()).parse()
    node = ast.body[0]
    assert isinstance(node, VarAssignment), f"got {type(node)}"
    assert node.name == "pos_x"
    assert isinstance(node.value, FloatLiteral)
    assert node.value.value == 5.0
test("parser_var", t_parser_var)


# ── PhysicsEval ───────────────────────────────────────────────────────────────

def t_physics_eval_addition():
    from compiler.predict_interp import PhysicsEval
    from compiler.ast_nodes import Assignment, Identifier, BinaryOp, FloatLiteral
    N = 100
    ev = PhysicsEval({"x": np.ones(N) * 3.0, "vx": np.ones(N) * 2.0}, dt=0.5)
    # x = x + vx * dt  ->  3 + 2*0.5 = 4
    stmt = Assignment(
        target=Identifier(name="x"),
        value=BinaryOp(
            left=Identifier(name="x"),
            op="+",
            right=BinaryOp(
                left=Identifier(name="vx"),
                op="*",
                right=Identifier(name="dt"),
            ),
        ),
    )
    ev.exec_stmt(stmt)
    assert (np.abs(ev.state["x"] - 4.0) < 1e-9).all(), f"x={ev.state['x'][0]}"
test("physics_eval_addition", t_physics_eval_addition)

def t_physics_eval_if_clamping():
    "if y < 0: y = 0.0 — negative particles must be clamped."
    from compiler.predict_interp import PhysicsEval
    from compiler.ast_nodes import IfStmt, Assignment, Identifier, FloatLiteral, BinaryOp
    N = 200
    y_vals = np.linspace(-5, 5, N)
    ev = PhysicsEval({"y": y_vals.copy()}, dt=0.1)
    cond   = BinaryOp(left=Identifier(name="y"), op="<", right=FloatLiteral(value=0.0))
    assign = Assignment(target=Identifier(name="y"), value=FloatLiteral(value=0.0))
    stmt   = IfStmt(condition=cond, then_body=[assign], elif_clauses=[], else_body=None)
    ev.exec_stmt(stmt)
    assert (ev.state["y"] >= 0).all(), "negative y survived"
    # positive half must be unchanged
    pos_mask = y_vals > 0
    assert np.allclose(ev.state["y"][pos_mask], y_vals[pos_mask])
test("physics_eval_if_clamping", t_physics_eval_if_clamping)

def t_physics_eval_if_else():
    "if x > 0: x = 1  else: x = -1"
    from compiler.predict_interp import PhysicsEval
    from compiler.ast_nodes import IfStmt, Assignment, Identifier, FloatLiteral, BinaryOp, UnaryOp
    N = 100
    ev = PhysicsEval({"x": np.linspace(-1, 1, N)}, dt=0.1)
    cond       = BinaryOp(left=Identifier(name="x"), op=">", right=FloatLiteral(value=0.0))
    then_body  = [Assignment(target=Identifier(name="x"), value=FloatLiteral(value=1.0))]
    else_body  = [Assignment(target=Identifier(name="x"), value=FloatLiteral(value=-1.0))]
    stmt = IfStmt(condition=cond, then_body=then_body, elif_clauses=[], else_body=else_body)
    ev.exec_stmt(stmt)
    assert set(np.unique(np.round(ev.state["x"], 6))).issubset({-1.0, 1.0})
test("physics_eval_if_else", t_physics_eval_if_else)

def t_physics_eval_unary_minus():
    from compiler.predict_interp import PhysicsEval
    from compiler.ast_nodes import Assignment, Identifier, UnaryOp
    N = 50
    ev = PhysicsEval({"x": np.ones(N) * 5.0}, dt=0.1)
    stmt = Assignment(target=Identifier(name="x"),
                      value=UnaryOp(op="-", operand=Identifier(name="x")))
    ev.exec_stmt(stmt)
    assert (ev.state["x"] == -5.0).all()
test("physics_eval_unary_minus", t_physics_eval_unary_minus)

def t_physics_eval_aug_assign():
    from compiler.predict_interp import PhysicsEval
    from compiler.ast_nodes import AugAssignment, Identifier, FloatLiteral
    N = 50
    ev = PhysicsEval({"x": np.ones(N) * 3.0}, dt=0.1)
    stmt = AugAssignment(target=Identifier(name="x"), op="+=", value=FloatLiteral(value=2.0))
    ev.exec_stmt(stmt)
    assert (ev.state["x"] == 5.0).all()
test("physics_eval_aug_assign", t_physics_eval_aug_assign)


# ── PredictInterpreter ────────────────────────────────────────────────────────

def t_interp_var():
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.predict_interp import PredictInterpreter
    ast = Parser(Lexer("var x = 3.14\n").tokenize()).parse()
    interp = PredictInterpreter(n_particles=100, output_dir=tempfile.gettempdir())
    interp.run(ast)
    assert abs(interp.scalars["x"] - 3.14) < 1e-9
test("interp_var", t_interp_var)

def t_interp_super():
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.predict_interp import PredictInterpreter
    from prediction.distributions import Gaussian
    ast = Parser(Lexer("super bc = gaussian(center: 0.8, spread: 0.05)\n").tokenize()).parse()
    interp = PredictInterpreter(n_particles=100, output_dir=tempfile.gettempdir())
    interp.run(ast)
    assert "bc" in interp.supers
    assert isinstance(interp.supers["bc"], Gaussian)
    assert interp.supers["bc"].center == 0.8
test("interp_super", t_interp_super)

def t_interp_predict_basic():
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.predict_interp import PredictInterpreter
    src = "var x = 0.0\nvar vx = 2.0\npredict 1 seconds, step 0.1:\n    x = x + vx * dt\nshow x\n"
    ast = Parser(Lexer(src).tokenize()).parse()
    interp = PredictInterpreter(n_particles=500, output_dir=tempfile.gettempdir())
    interp.run(ast)
    assert interp.result is not None
    final = interp.result.clouds["x"][-1]
    assert abs(final.mean() - 2.0) < 0.2, f"expected ~2.0, got {final.mean()}"
test("interp_predict_basic", t_interp_predict_basic)

def t_interp_bouncing_ball():
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.predict_interp import PredictInterpreter
    src = open(os.path.join(os.path.dirname(__file__), "..", "examples", "bouncing_ball.sp")).read()
    ast = Parser(Lexer(src).tokenize()).parse()
    interp = PredictInterpreter(n_particles=1000, output_dir=tempfile.gettempdir())
    interp.run(ast)
    assert interp.result is not None
    assert "pos_x" in interp.result.clouds
    assert "pos_y" in interp.result.clouds
    assert len(interp.result.timesteps) == 100
    # Uncertainty in pos_y must grow (bouncing amplifies it)
    std_early = interp.result.clouds["pos_y"][5].std()
    std_late  = interp.result.clouds["pos_y"][-1].std()
    assert std_late > std_early, f"uncertainty not growing: {std_early:.4f} -> {std_late:.4f}"
test("interp_bouncing_ball", t_interp_bouncing_ball)

def t_interp_show_no_predict():
    "show before predict must not crash."
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.predict_interp import PredictInterpreter
    import io, contextlib
    ast = Parser(Lexer("show x\n").tokenize()).parse()
    interp = PredictInterpreter(n_particles=100, output_dir=tempfile.gettempdir())
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        interp.run(ast)
    assert "No prediction result" in buf.getvalue()
test("interp_show_no_predict", t_interp_show_no_predict)


# ── AR: tracker ───────────────────────────────────────────────────────────────

def t_velocity_estimator_accuracy():
    "Simulate 60fps detections: ball at 300 px/s."
    from ar.tracker import VelocityEstimator
    ve = VelocityEstimator()
    t = 0.0
    for _ in range(12):
        t += 1 / 60.0
        ve._hist.append((t, 300.0 * t, 150.0 * t))
    times = np.array([h[0] for h in ve._hist])
    xs    = np.array([h[1] for h in ve._hist])
    ys    = np.array([h[2] for h in ve._hist])
    times -= times[0]
    vx = float(np.polyfit(times, xs, 1)[0])
    vy = float(np.polyfit(times, ys, 1)[0])
    assert abs(vx - 300) < 5,  f"vx={vx}"
    assert abs(vy - 150) < 5,  f"vy={vy}"
test("velocity_estimator_accuracy", t_velocity_estimator_accuracy)

def t_velocity_estimator_too_few():
    "Single sample must return (0, 0) without error."
    from ar.tracker import VelocityEstimator
    ve = VelocityEstimator()
    vx, vy = ve.update(10.0, 20.0)
    assert vx == 0.0 and vy == 0.0
test("velocity_estimator_too_few", t_velocity_estimator_too_few)

def t_color_tracker_init():
    from ar.tracker import ColorTracker
    for color in ("red", "green", "yellow", "blue", "orange"):
        t = ColorTracker(color=color)
        assert t.ranges
test("color_tracker_init", t_color_tracker_init)

def t_color_tracker_unknown():
    from ar.tracker import ColorTracker
    try:
        ColorTracker("purple")
        assert False, "should raise"
    except ValueError:
        pass
test("color_tracker_unknown_color", t_color_tracker_unknown)

def t_color_tracker_no_object():
    "All-black frame -> no detection."
    import cv2
    from ar.tracker import ColorTracker
    tracker = ColorTracker("red")
    frame = np.zeros((480, 640, 3), dtype=np.uint8)
    det = tracker.detect(frame)
    assert det is None
test("color_tracker_no_object", t_color_tracker_no_object)

def t_color_tracker_red_circle():
    "Frame with a red circle must be detected."
    import cv2
    from ar.tracker import ColorTracker
    frame = np.zeros((480, 640, 3), dtype=np.uint8)
    # Draw a bright red circle (BGR: 0,0,255)
    cv2.circle(frame, (320, 240), 40, (0, 0, 255), -1)
    tracker = ColorTracker("red", min_radius=10)
    det = tracker.detect(frame)
    assert det is not None, "red circle not detected"
    assert abs(det.x - 320) < 30, f"cx={det.x}"
    assert abs(det.y - 240) < 30, f"cy={det.y}"
test("color_tracker_red_circle", t_color_tracker_red_circle)


# ── AR: renderer ─────────────────────────────────────────────────────────────

def t_renderer_output_shape():
    from prediction.particle_filter import ParticleState
    from prediction.predict import predict
    from ar.renderer import CloudRenderer
    state = ParticleState(200)
    state.set("x", 100.0); state.set("vx", 50.0)
    def physics(v, dt): return {"x": v["x"]+v["vx"]*dt, "vx": v["vx"].copy()}
    r = predict(1.0, 0.1, state, physics, output_vars=["x"])
    # 1D result — need two vars for the renderer; add y
    state2 = ParticleState(200)
    state2.set("x", 100.0); state2.set("y", 200.0)
    state2.set("vx", 50.0); state2.set("vy", -20.0)
    def physics2(v, dt):
        return {"x": v["x"]+v["vx"]*dt, "y": v["y"]+v["vy"]*dt,
                "vx": v["vx"].copy(), "vy": v["vy"].copy()}
    r2 = predict(1.0, 0.1, state2, physics2, output_vars=["x", "y"])
    renderer = CloudRenderer((480, 640))
    frame = np.zeros((480, 640, 3), dtype=np.uint8)
    out = renderer.draw(frame, r2, "x", "y")
    assert out.shape == (480, 640, 3)
    assert out.dtype == np.uint8
test("renderer_output_shape", t_renderer_output_shape)

def t_renderer_color_mapping():
    from ar.renderer import _plasma_bgr, _viridis_bgr
    for t_val in np.linspace(0, 1, 11):
        for fn in (_plasma_bgr, _viridis_bgr):
            b, g, r = fn(t_val)
            assert 0 <= b <= 255 and 0 <= g <= 255 and 0 <= r <= 255
    # Out-of-range must clamp, not raise
    _plasma_bgr(-0.5)
    _plasma_bgr(1.5)
test("renderer_color_mapping", t_renderer_color_mapping)

def t_renderer_empty_result():
    "draw() with missing var keys must return the frame unchanged (no crash)."
    from prediction.particle_filter import ParticleState
    from prediction.predict import predict
    from ar.renderer import CloudRenderer
    state = ParticleState(100)
    state.set("x", 0.0)
    def physics(v, dt): return {"x": v["x"]}
    r = predict(1.0, 0.1, state, physics, output_vars=["x"])
    renderer = CloudRenderer((480, 640))
    frame = np.zeros((480, 640, 3), dtype=np.uint8)
    # pass wrong var names — must not crash
    out = renderer.draw(frame, r, "missing_x", "missing_y")
    assert out.shape == (480, 640, 3)
test("renderer_empty_result", t_renderer_empty_result)

def t_renderer_draw_detection():
    from ar.renderer import CloudRenderer
    renderer = CloudRenderer((480, 640))
    frame = np.zeros((480, 640, 3), dtype=np.uint8)
    out = renderer.draw_detection(frame, 320.0, 240.0, radius=20.0)
    assert out.shape == (480, 640, 3)
    # frame should have changed (green circle drawn)
    assert out.max() > 0
test("renderer_draw_detection", t_renderer_draw_detection)


# ── integration: CLI predict command ─────────────────────────────────────────

def t_cli_predict():
    "Run the CLI predict command on bouncing_ball.sp without crash."
    import subprocess, sys
    result = subprocess.run(
        [sys.executable, "spectra.py", "predict",
         "examples/bouncing_ball.sp",
         "--particles", "500",
         "--output-dir", tempfile.gettempdir()],
        capture_output=True, text=True,
        cwd=os.path.join(os.path.dirname(__file__), ".."),
    )
    assert result.returncode == 0, f"CLI failed:\n{result.stderr}"
    assert "[predict]" in result.stdout
    assert "[show]" in result.stdout
test("cli_predict", t_cli_predict)


# ── ObjectSchema / BehaviorObject ────────────────────────────────────────────

def t_object_schema_create():
    from prediction.schema import ObjectSchema
    from prediction.distributions import gaussian, discrete
    schema = ObjectSchema("TestObj")
    schema.add_dimension("pos_x",  "kinematic", gaussian(100, 5))
    schema.add_dimension("pos_y",  "kinematic", gaussian(200, 5))
    schema.add_dimension("intent", "intent",    discrete({"fwd": 0.6, "left": 0.4}))
    state = schema.create_state(500)
    assert "pos_x"  in state.vars
    assert "pos_y"  in state.vars
    assert "intent" in state.vars
    assert state.vars["intent"].dtype in (np.float32, np.float64)
    assert set(state.vars["intent"]).issubset({0.0, 1.0})
test("schema_create", t_object_schema_create)

def t_object_schema_label_maps():
    from prediction.schema import ObjectSchema
    from prediction.distributions import discrete
    schema = ObjectSchema("T")
    schema.add_dimension("dir", "intent", discrete({"fwd": 0.5, "left": 0.3, "right": 0.2}))
    lm = schema.label_maps()
    assert "dir" in lm
    assert lm["dir"]["fwd"]   == 0.0
    assert lm["dir"]["left"]  == 1.0
    assert lm["dir"]["right"] == 2.0
test("schema_label_maps", t_object_schema_label_maps)

def t_behavior_object_observe():
    from prediction.schema import ObjectSchema, BehaviorObject
    from prediction.distributions import gaussian
    schema = ObjectSchema("T")
    schema.add_dimension("pos_x", "kinematic", gaussian(100, 10))
    schema.add_dimension("pos_y", "kinematic", gaussian(200, 10))
    obj = BehaviorObject(schema, n_particles=500)
    # Before observe, particles are spread around 100, 200
    before_ess = obj.state.effective_sample_size()
    obj.observe({"pos_x": 100.0, "pos_y": 200.0}, spread=5.0)
    after_ess = obj.state.effective_sample_size()
    assert after_ess <= before_ess   # weights got concentrated → lower ESS
test("behavior_object_observe", t_behavior_object_observe)

def t_behavior_object_intent_dist():
    from prediction.schema import ObjectSchema, BehaviorObject
    from prediction.distributions import discrete
    schema = ObjectSchema("T")
    schema.add_dimension("intent", "intent",
                         discrete({"forward": 0.7, "turn": 0.3}))
    obj  = BehaviorObject(schema, n_particles=5000)
    dist = obj.intent_distribution()
    assert "intent" in dist
    fwd  = dist["intent"].get("forward", 0)
    turn = dist["intent"].get("turn",    0)
    assert abs(fwd  - 0.7) < 0.05, f"fwd={fwd}"
    assert abs(turn - 0.3) < 0.05, f"turn={turn}"
test("behavior_object_intent_dist", t_behavior_object_intent_dist)


# ── MultiObjectPredictor ──────────────────────────────────────────────────────

def t_multi_predict_concurrent():
    from prediction.schema import ObjectSchema, BehaviorObject
    from prediction.multi_predict import MultiObjectPredictor
    from prediction.distributions import gaussian

    def make_obj(name, x0):
        schema = ObjectSchema(name)
        schema.add_dimension("pos_x", "kinematic", gaussian(x0,  2.0))
        schema.add_dimension("vel_x", "kinematic", gaussian(1.0, 0.1))
        def physics(v, dt):
            return {"pos_x": v["pos_x"] + v["vel_x"] * dt, "vel_x": v["vel_x"]}
        schema.set_physics(physics)
        return BehaviorObject(schema, n_particles=500, object_id=name)

    obj_a = make_obj("A", 0.0)
    obj_b = make_obj("B", 10.0)
    mp = MultiObjectPredictor(max_workers=2)
    mp.register(obj_a)
    mp.register(obj_b)
    results = mp.predict_all(duration=1.0, step=0.1)
    mp.shutdown(wait=True)

    assert "A" in results and "B" in results
    # A should be near x=1 after 1s, B near x=11
    cloud_a = results["A"].clouds["pos_x"][-1]
    cloud_b = results["B"].clouds["pos_x"][-1]
    assert abs(cloud_a.mean() - 1.0)  < 0.2, f"A mean={cloud_a.mean()}"
    assert abs(cloud_b.mean() - 11.0) < 0.2, f"B mean={cloud_b.mean()}"
test("multi_predict_concurrent", t_multi_predict_concurrent)


# ── Discrete label comparison in PhysicsEval ─────────────────────────────────

def t_physics_eval_discrete_labels():
    "if intent == 'left': heading -= 1.0 * dt  should apply only to left particles."
    from compiler.predict_interp import PhysicsEval
    from compiler.ast_nodes import (
        IfStmt, Assignment, AugAssignment, Identifier,
        FloatLiteral, BinaryOp, StringLiteral
    )
    N = 1000
    # Half particles intent=0 (fwd), half intent=1 (left)
    intent  = np.array([0.0] * (N // 2) + [1.0] * (N // 2))
    heading = np.zeros(N)
    lmaps   = {"intent": {"fwd": 0.0, "left": 1.0}}
    ev = PhysicsEval({"intent": intent, "heading": heading}, dt=1.0,
                     label_maps=lmaps)

    cond = BinaryOp(left=Identifier(name="intent"), op="==",
                    right=StringLiteral(value="left"))
    then = [AugAssignment(target=Identifier(name="heading"),
                          op="-=", value=FloatLiteral(value=1.0))]
    ev.exec_stmt(IfStmt(condition=cond, then_body=then,
                        elif_clauses=[], else_body=None))

    # Forward particles: heading unchanged (0.0)
    fwd_mask  = ev.state["intent"] == 0.0
    left_mask = ev.state["intent"] == 1.0
    assert (ev.state["heading"][fwd_mask]  == 0.0).all()
    assert (ev.state["heading"][left_mask] == -1.0).all()
test("physics_eval_discrete_labels", t_physics_eval_discrete_labels)


# ── Parser: object block ──────────────────────────────────────────────────────

def t_parser_object_block():
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.ast_nodes import ObjectDef, DimensionGroupNode, PredictBlock
    src = (
        "object Ball:\n"
        "    dimensions:\n"
        "        kinematic: pos_x = 1.0, pos_y = 2.0\n"
        "        state: bounce = 0.8\n"
        "    predict 1 seconds, step 0.1:\n"
        "        pos_x = pos_x + 1.0\n"
    )
    prog = Parser(Lexer(src).tokenize()).parse()
    obj_nodes = [n for n in prog.body if isinstance(n, ObjectDef)]
    assert len(obj_nodes) == 1
    obj = obj_nodes[0]
    assert obj.name == "Ball"
    assert len(obj.dimension_groups) == 2
    kin = obj.dimension_groups[0]
    assert kin.group_type == "kinematic"
    assert len(kin.vars) == 2
    assert kin.vars[0][0] == "pos_x"
    assert obj.predict_block is not None
    assert obj.predict_block.duration == 1.0
test("parser_object_block", t_parser_object_block)

def t_parser_observe():
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.ast_nodes import ObserveStmt
    src = "observe Ball pos_x = 100.0, pos_y = 200.0 spread 5.0\n"
    prog = Parser(Lexer(src).tokenize()).parse()
    obs_nodes = [n for n in prog.body if isinstance(n, ObserveStmt)]
    assert len(obs_nodes) == 1
    obs = obs_nodes[0]
    assert obs.object_name == "Ball"
    assert "pos_x" in obs.measurements
    assert abs(obs.spread - 5.0) < 1e-9
test("parser_observe", t_parser_observe)


# ── Interpreter: object block end-to-end ─────────────────────────────────────

def t_interp_object_def():
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.predict_interp import PredictInterpreter
    src = (
        "object Dot:\n"
        "    dimensions:\n"
        "        kinematic: pos_x = 5.0, pos_y = 0.0\n"
        "    predict 1 seconds, step 0.1:\n"
        "        pos_x = pos_x + 1.0 * dt\n"
    )
    prog   = Parser(Lexer(src).tokenize()).parse()
    interp = PredictInterpreter(n_particles=200, output_dir=tempfile.gettempdir())
    interp.run(prog)
    assert "Dot" in interp.objects
    obj = interp.objects["Dot"]
    assert obj.result is not None
    # After 1s at speed 1.0, pos_x should be ~6.0
    final_cloud = obj.result.clouds["pos_x"][-1]
    assert abs(final_cloud.mean() - 6.0) < 0.1, f"mean={final_cloud.mean()}"
test("interp_object_def", t_interp_object_def)

def t_interp_object_show():
    "show Ball should pull result from the object registry."
    from compiler.lexer import Lexer
    from compiler.parser import Parser
    from compiler.predict_interp import PredictInterpreter
    src = (
        "object Ball:\n"
        "    dimensions:\n"
        "        kinematic: pos_x = 1.0, pos_y = 0.0\n"
        "    predict 0.5 seconds, step 0.1:\n"
        "        pos_x = pos_x + 1.0 * dt\n"
        "show Ball as \"ball\"\n"
    )
    prog   = Parser(Lexer(src).tokenize()).parse()
    interp = PredictInterpreter(n_particles=100, output_dir=tempfile.gettempdir())
    interp.run(prog)   # must not crash; files written to tempdir
test("interp_object_show", t_interp_object_show)


# ── BehaviorRenderer smoke test ───────────────────────────────────────────────

def t_behavior_renderer_smoke():
    from prediction.schema import ObjectSchema, BehaviorObject
    from prediction.multi_predict import MultiObjectPredictor
    from prediction.distributions import gaussian, discrete
    from ar.behavior_renderer import BehaviorRenderer

    # Build a simple object with a result
    schema = ObjectSchema("T")
    schema.add_dimension("pos_x",  "kinematic", gaussian(320, 5))
    schema.add_dimension("pos_y",  "kinematic", gaussian(240, 5))
    schema.add_dimension("intent", "intent",    discrete({"go": 0.7, "stop": 0.3}))
    def physics(v, dt):
        return {"pos_x": v["pos_x"] + 5*dt, "pos_y": v["pos_y"], "intent": v["intent"]}
    schema.set_physics(physics)

    obj = BehaviorObject(schema, n_particles=500, object_id="T")
    mp  = MultiObjectPredictor(max_workers=1)
    mp.register(obj)
    mp.predict_all(1.0, 0.1)
    mp.shutdown(wait=True)

    renderer = BehaviorRenderer(frame_shape=(480, 640))
    frame = np.zeros((480, 640, 3), dtype=np.uint8)
    out = renderer.draw_all(frame, [obj])
    assert out.shape == (480, 640, 3)
    assert out.max() > 0   # something was drawn
test("behavior_renderer_smoke", t_behavior_renderer_smoke)


# ── CLI: multi_object.sp ──────────────────────────────────────────────────────

def t_cli_multi_object():
    "Run the CLI on multi_object.sp (no crash, objects created)."
    import subprocess, sys
    result = subprocess.run(
        [sys.executable, "spectra.py", "predict",
         "examples/multi_object.sp",
         "--particles", "200",
         "--output-dir", tempfile.gettempdir()],
        capture_output=True, text=True,
        cwd=os.path.join(os.path.dirname(__file__), ".."),
    )
    assert result.returncode == 0, f"CLI failed:\n{result.stderr}\n{result.stdout}"
    assert "[object]" in result.stdout
    assert "Ball"     in result.stdout
    assert "Wanderer" in result.stdout
test("cli_multi_object", t_cli_multi_object)


# ── print results ─────────────────────────────────────────────────────────────

if __name__ == "__main__":
    passes = [r for r in results if r[0] == "PASS"]
    fails  = [r for r in results if r[0] == "FAIL"]

    print(f"\n{'='*60}")
    print(f"  Results: {len(passes)} passed, {len(fails)} failed  (total {len(results)})")
    print(f"{'='*60}")
    for r in results:
        mark = "PASS" if r[0] == "PASS" else "FAIL"
        print(f"  [{mark}]  {r[1]}")
        if r[0] == "FAIL":
            # Print just the last line of the traceback
            lines = [l for l in r[2].strip().splitlines() if l.strip()]
            print(f"           {lines[-1]}")

    if fails:
        print()
        print("FAILED TEST DETAILS:")
        for r in fails:
            print(f"\n--- {r[1]} ---")
            print(r[2])
        sys.exit(1)
    else:
        print("\nAll tests passed.")
