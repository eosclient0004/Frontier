import assert from "node:assert/strict";
import {
  BakeSceneSdf,
  EvaluatePrimitiveSdf,
  SampleSdfTrilinear,
  SceneSdfSignature,
  SerializeSdfVolume,
} from "./SdfVolumeSystem.js";

const Box = {
  Id: "unit-box",
  Shape: 0,
  Center: [0, 0, 0],
  RotationRad: [0, 0, 0],
  HalfExtents: [0.5, 0.5, 0.5],
  GiContributor: 1,
};
assert.ok(EvaluatePrimitiveSdf([0, 0, 0], Box) < 0, "box centre is inside");
assert.ok(EvaluatePrimitiveSdf([1, 0, 0], Box) > 0, "point beyond box is outside");

const Scene = { Primitives: [Box], Vehicles: [] };
const Volume = await BakeSceneSdf(Scene, { SdfResolution: 32, SdfPadding: 0.15 }, () => {});
assert.equal(Volume.Resolution, 32);
assert.equal(Volume.Values.length, 32 ** 3);
assert.equal(Volume.Signed, true);
assert.ok(Volume.MinimumDistance < 0 && Volume.MaximumDistance > 0, "volume retains signed interior and exterior");
assert.ok(SampleSdfTrilinear(Volume, [0, 0, 0]) < 0, "trilinear sample preserves inside sign");
assert.ok(SampleSdfTrilinear(Volume, [1, 0, 0]) > 0, "trilinear sample preserves outside sign");
assert.equal(Volume.Signature, SceneSdfSignature(Scene, { SdfResolution: 32, SdfPadding: 0.15 }));

const Binary = SerializeSdfVolume(Volume);
const View = new DataView(Binary);
assert.equal(View.getUint32(0, true), 0x46534446, "FSDF magic");
assert.ok(View.getUint32(4, true) > 20, "FSDF metadata header exists");
console.log("[SdfVolumeSystem] signed bake, sampling, signature, and FSDF export passed");
