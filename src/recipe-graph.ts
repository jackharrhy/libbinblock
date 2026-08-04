import type { ExpressionValue, Stage } from './recipe-schema.js';

export type StageBindings = Record<string, unknown>;
export type StageArtifacts = ReadonlyMap<string, readonly unknown[]>;
export type AxisMatcher = (where: ExpressionValue, bindings: StageBindings) => boolean;

export function stageDependencies(stage: Stage): string[] {
  return Object.values(stage.forEach)
    .filter((axis) => axis.source === 'stage')
    .map((axis) => axis.stage);
}

export function sortRecipeStages(stages: readonly Stage[]): Stage[] {
  const byId = new Map(stages.map((stage) => [stage.id, stage]));
  const ordered: Stage[] = [];
  const visited = new Set<string>();
  const visiting = new Set<string>();

  function visit(stage: Stage): void {
    if (visited.has(stage.id)) return;
    if (visiting.has(stage.id)) throw new Error(`Stage dependency cycle at ${stage.id}.`);
    visiting.add(stage.id);
    for (const dependencyId of stageDependencies(stage)) {
      const dependency = byId.get(dependencyId);
      if (!dependency) throw new Error(`Stage ${stage.id} depends on unknown stage ${dependencyId}.`);
      visit(dependency);
    }
    visiting.delete(stage.id);
    visited.add(stage.id);
    ordered.push(stage);
  }

  for (const stage of stages) visit(stage);
  return ordered;
}

export function expandStageBindings(stage: Stage, artifactsByStage: StageArtifacts, matches: AxisMatcher = () => true): StageBindings[] {
  let combinations: StageBindings[] = [{}];
  for (const [binding, axis] of Object.entries(stage.forEach)) {
    const values = axis.source === 'values' ? axis.values : (artifactsByStage.get(axis.stage) ?? []);
    combinations = combinations.flatMap((combination) =>
      values
        .map((value) => ({ ...combination, [binding]: value }))
        .filter((bindings) => axis.where === undefined || matches(axis.where, bindings)),
    );
  }
  return combinations;
}
