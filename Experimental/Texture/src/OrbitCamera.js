import { Clamp, Mat4 } from "./MathLibrary.js";

// Damped orbit camera. Targets are eased toward each frame so trackpad and
// mouse input feel the same as the Fluid editor's viewport.
export class OrbitCamera {
  constructor() {
    this.Reset();
    this.Yaw = this.TargetYaw;
    this.Pitch = this.TargetPitch;
    this.Distance = this.TargetDistance;
    this.Center = [...this.TargetCenter];
    this.FieldOfView = (38 * Math.PI) / 180;
  }
  Reset() {
    this.TargetYaw = -0.62;
    this.TargetPitch = 0.32;
    this.TargetDistance = 3.6;
    this.TargetCenter = [0, 0, 0];
  }
  Serialize() {
    return {
      Yaw: this.TargetYaw,
      Pitch: this.TargetPitch,
      Distance: this.TargetDistance,
      Center: [...this.TargetCenter],
    };
  }
  Restore(State, Snap = false) {
    if (!State) return;
    this.TargetYaw = State.Yaw;
    this.TargetPitch = State.Pitch;
    this.TargetDistance = State.Distance;
    this.TargetCenter = [...State.Center];
    if (Snap) this.Snap();
  }
  Snap() {
    this.Yaw = this.TargetYaw;
    this.Pitch = this.TargetPitch;
    this.Distance = this.TargetDistance;
    this.Center = [...this.TargetCenter];
  }
  Orbit(DeltaX, DeltaY) {
    this.TargetYaw -= DeltaX * 0.0062;
    this.TargetPitch = Clamp(this.TargetPitch + DeltaY * 0.0062, -1.52, 1.52);
  }
  Pan(DeltaX, DeltaY, ViewportHeight) {
    const Scale =
      (2 * this.Distance * Math.tan(this.FieldOfView / 2)) /
      Math.max(1, ViewportHeight);
    const { Right, Up } = this.Basis();
    for (let Axis = 0; Axis < 3; Axis++)
      this.TargetCenter[Axis] +=
        (-DeltaX * Right[Axis] + DeltaY * Up[Axis]) * Scale;
  }
  Zoom(Delta) {
    this.TargetDistance = Clamp(
      this.TargetDistance * Math.exp(Delta * 0.0012),
      0.6,
      16,
    );
  }
  Update(DeltaTime) {
    const T = 1 - Math.exp(-DeltaTime * 18);
    const Before = [this.Yaw, this.Pitch, this.Distance, ...this.Center];
    this.Yaw += (this.TargetYaw - this.Yaw) * T;
    this.Pitch += (this.TargetPitch - this.Pitch) * T;
    this.Distance += (this.TargetDistance - this.Distance) * T;
    for (let Axis = 0; Axis < 3; Axis++)
      this.Center[Axis] += (this.TargetCenter[Axis] - this.Center[Axis]) * T;
    const After = [this.Yaw, this.Pitch, this.Distance, ...this.Center];
    const Moving = After.some((Value, Index) => Math.abs(Value - Before[Index]) > 1e-5);
    if (!Moving) this.Snap();
    return Moving;
  }
  Eye() {
    const CosPitch = Math.cos(this.Pitch);
    return [
      this.Center[0] + this.Distance * CosPitch * Math.sin(this.Yaw),
      this.Center[1] + this.Distance * Math.sin(this.Pitch),
      this.Center[2] + this.Distance * CosPitch * Math.cos(this.Yaw),
    ];
  }
  Basis() {
    const View = this.View();
    return {
      Right: [View[0], View[4], View[8]],
      Up: [View[1], View[5], View[9]],
      Forward: [-View[2], -View[6], -View[10]],
    };
  }
  View() {
    return Mat4.LookAt(this.Eye(), this.Center, [0, 1, 0]);
  }
  // Portrait panes keep the horizontal field of view so the model never crops.
  Projection(Aspect) {
    const Vertical = Aspect >= 1 ? this.FieldOfView : 2 * Math.atan(Math.tan(this.FieldOfView / 2) / Aspect);
    return Mat4.Perspective(Math.min(Vertical, 2.6), Aspect, 0.05, 60);
  }
}
