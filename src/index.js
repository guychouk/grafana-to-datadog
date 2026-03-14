const LAYOUT_TYPES = { ORDERED: "ordered", FREE: "free" };

// promql -> datadog
const FUNCTION_MAP = {
  abs: "abs",
  clamp_min: "clamp_min",
  clamp_max: "clamp_max",
  deriv: "derivative",
  log2: "log2",
  log10: "log10",
  log10: "log10",
  delta: "dt",
  rate: "per_second",
};

function parseJson(json) {
  try {
    return JSON.parse(json);
  } catch (err) {
    console.error(err);
    return {};
  }
}

function convertTemplatingToTemplateVariables(templating) {
  const { name } = templating;

  return {
    name: name.toLowerCase(),
    prefix: name.toLowerCase(),
  };
}

function parsePromQl(query) {
  try {
    return peg$parse(query);
  } catch (err) {
    return { error: err, query: query };
  }
}

function convertGrafanaSelectorToDatadog(selector) {
  const { label, op, value } = selector;
  // Datadog enables a shorthand for filtering with template variables:
  // metric_name{$org}
  switch (label) {
    case "organization":
      return "$organization";
    case "org":
      return "$org";
    default:
      // Datadog does not support:
      // - PromQL syntax for regex match (e.g. label~="4..")
      // - The catch all regex syntax (.*) (¯\_(ツ)_/¯)
      // This is why we need to "clean" the value
      return `${label}:${value
        .replace(/\.\./g, "*")
        .replace(/\.\*/g, "")
        .replace(/\/\//g, "/")}`;
  }
}

function convertPromQlToDatadog(expression) {
  if (typeof expression === "number") {
    return expression;
  }
  const { left, op, right, func, body, metric, aggregator } = expression;
  if (metric) {
    const { selectors = [] } = expression;
    const d_selectors = selectors.map(convertGrafanaSelectorToDatadog);
    const dd_selectors = d_selectors.length
      ? "{" + d_selectors.join(", ").toLowerCase() + "}"
      : "{*}";
    return `${metric}${dd_selectors}`;
  } else if (aggregator) {
    const { labels = [] } = expression;
    const aggBody = Array.isArray(body) ? body[0] : body;
    return { aggregator, query: convertPromQlToDatadog(aggBody), labels };
  } else if (left) {
    return {
      left: convertPromQlToDatadog(left),
      op,
      right: convertPromQlToDatadog(right),
    };
  } else if (func) {
    return {
      func: FUNCTION_MAP[func],
      args: body.slice(1),
      query: convertPromQlToDatadog(body[0]),
    };
  }
}

function parsePanel(panel) {
  const {
    type,
    title,
    panels = [],
    description = "",
    targets: queries = [],
  } = panel;
  if (type === "row") {
    return { title, type, children: panels.map(parsePanel) };
  }
  return {
    type,
    title,
    description,
    queries,
  };
}

function Query(str, name) {
  return { query: str, name, data_source: "metrics" };
}

function convertPanelToWidget(panel) {
  const {
    type,
    title,
    description,
    queries: promqls = [],
    children = [],
  } = panel;
  if (type === "text" || promqls.length === 0) {
    return false;
  }
  if (type === "row") {
    return {
      definition: {
        title,
        type: "group",
        layout_type: LAYOUT_TYPES.ORDERED,
        widgets: children.map(convertPanelToWidget),
      },
    };
  }
  const { queries, formulas } = _.map(promqls, "expr")
    .map(parsePromQl)
    .flatMap(convertPromQlToDatadog)
    .map(_massageQuery)
    .reduce(_reduceToQueriesAndFormulas, { queries: [], formulas: [] });

  return {
    definition: {
      title,
      type: "timeseries",
      // Why is requests an array? ¯\_(ツ)_/¯
      // https://docs.datadoghq.com/dashboards/graphing_json/request_json/
      requests: [
        {
          queries,
          formulas,
          response_format: "timeseries",
        },
      ],
    },
  };

  function _massageQuery(query, i) {
    if (_.isNumber(query)) {
      return query;
    } else if (_.isObject(query) && _.has(query, "aggregator")) {
      const { aggregator, query: q, labels } = query;
      const dd_grouping = labels.length
        ? " by {" + labels.join(", ") + "}"
        : "";
      const converted = _massageQuery(q, i);
      return {
        ...converted,
        query: `${aggregator}:${converted.query}${dd_grouping}`,
      };
    } else if (_.isObject(query) && _.has(query, "left")) {
      const { left, op, right } = query;
      return [_massageQuery(left, i), op, _massageQuery(right, i + 1)];
    } else if (_.isObject(query) && _.has(query, "func")) {
      const { query: q, func } = query;
      return { func, ...Query(q, `query${i}`) };
    } else {
      return query ? Query(query, `query${i}`) : undefined;
    }
  }

  function _reduceToQueriesAndFormulas(agg, query) {
    if (_.isArray(query)) {
      let f = "";
      _.flatten(query).forEach((q) => {
        if (_.isObject(q)) {
          agg.queries.push(q);
          f = `${f}${q.name}`;
        } else {
          f = `${f}${q}`;
        }
      });
      agg.formulas.push({ formula: f });
    } else if (_.isObject(query) && _.has(query, "func")) {
      const { query: q, name, data_source, func } = query;
      agg.queries.push({ query: q, name, data_source });
      agg.formulas.push({ formula: `${func}(${name})` });
    } else {
      if (query) {
        agg.queries.push(query);
        agg.formulas.push({ formula: query.name });
      }
    }
    return agg;
  }
}

function GrafanaToDatadog(json) {
    const { title, tags, templating = [], panels } = parseJson(json);

    const widgets = panels
      .map(parsePanel)
      .map(convertPanelToWidget)
      .filter(Boolean);

    const template_variables = templating.list.map(
      convertTemplatingToTemplateVariables
    );

    return {
      title,
      widgets,
      description: "",
      layout_type: LAYOUT_TYPES.ORDERED,
      is_read_only: false,
      template_variables,
      notify_list: [],
    };
}

function Toaster(message) {
  const toaster = document.querySelector('.toaster');
  toaster.innerHTML = `<p>${message}</p>`
  toaster.classList.add('fade-in');
  setTimeout(function() {
    toaster.classList.remove('fade-in');
  }, 1500);
}

(function () {
  let cachedJson;

  window.addEventListener('paste', function(evt) {
    document.querySelector('.json-output').classList.remove('banner');
    const text = evt.clipboardData.getData('text/plain');
    cachedJson = JSON.stringify(GrafanaToDatadog(text), null, 2);
    datadog.innerHTML = cachedJson;
    Prism.highlightAll();
    Toaster("Converted Grafana JSON to Datadog JSON!");
  });
  window.addEventListener('copy', function(evt) {
    if (!cachedJson) {
      return;
    }
    evt.clipboardData.setData('text/plain', cachedJson);
    evt.preventDefault();
    Toaster("Copied JSON to clipboard!");
  });
})();
