/**
 * Deterministic Authored Geological Parameter Selector (PRNG)
 * 
 * STRICT GEOLOGICAL CONSTRAINT COMPLIANCE:
 * - NO continuous random fields or heightmaps.
 * - NO vertex displacement via pseudo-random gradients.
 * - This module ONLY provides discrete selection and parameter arrangement for
 *   authored structural geological primitives (joint sets, strata layers, cutter bounds).
 */

export class GeologicalPRNG {
  private state: number;

  constructor(seed: number) {
    this.state = (seed ^ 0x6c62272e) >>> 0;
    if (this.state === 0) this.state = 0x12345678;
  }

  /**
   * Mulberry32 32-bit deterministic PRNG
   * Returns a float in [0, 1)
   */
  public nextFloat(): number {
    let t = (this.state += 0x6d2b79f5);
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  }

  /**
   * Select a float in range [min, max]
   */
  public range(min: number, max: number): number {
    return min + this.nextFloat() * (max - min);
  }

  /**
   * Select an integer in range [min, max] inclusive
   */
  public rangeInt(min: number, max: number): number {
    return Math.floor(this.range(min, max + 1));
  }

  /**
   * Select a random element from an authored discrete array
   */
  public choose<T>(array: readonly T[]): T {
    if (array.length === 0) throw new Error('Cannot choose from empty array');
    const index = Math.floor(this.nextFloat() * array.length);
    return array[index];
  }

  /**
   * Shuffle an array deterministically (Fisher-Yates)
   */
  public shuffle<T>(array: T[]): T[] {
    const copy = [...array];
    for (let i = copy.length - 1; i > 0; i--) {
      const j = Math.floor(this.nextFloat() * (i + 1));
      [copy[i], copy[j]] = [copy[j], copy[i]];
    }
    return copy;
  }

  /**
   * Discrete boolean with probability p
   */
  public chance(probability: number): boolean {
    return this.nextFloat() < probability;
  }
}
