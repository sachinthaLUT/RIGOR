%% RIGOR_figures.m
% Publication figures and table values for "RIGOR: Reconfigurable
% Electrically Actuated Testing Rig for Compressive Loading", read directly
% from ../calibration/Force_Rig_Calibration_Workbook.xlsx.
%
% Everything is recomputed from the raw (green, pasted) cells of the
% workbook, so the output stays correct if you re-run a test and re-paste.
%
% Output (folder cfg.outDir, default 'figures'):
%   Fig_*.pdf   vector figures for \includegraphics (upload the folder to Overleaf)
%   Fig_*.png   600 dpi previews
%   paper_table_values.txt   every number for the tables of main.tex,
%                            labelled with the table number and \label
%
% Requires MATLAB R2020a or newer (readmatrix, tiledlayout, exportgraphics).
%
% Figure -> main.tex placement (the \figorbox lines already point at these)
%   Fig_CalResiduals     Sec 4.2.4  fig:cal:residuals   (Figure 5)
%   Fig_CommandAccuracy  Sec 4.3    fig:command_accuracy
%   Fig_Repeatability    Sec 4.4.2  fig:repeatability
%   Fig_Direction        Sec 4.5.2  fig:direction
%   Fig_WarmupDrift      Sec 4.6.1  fig:warmup
%   Fig_Creep            Sec 4.6.2  fig:creep
%   Fig_StepResponse     Sec 4.7.1  fig:step
%   Fig_Cyclic           Sec 4.7.2  fig:cal:transmission
%   Fig_CurrentForce     Sec 4.8    fig:current_force
%   Fig_Uncertainty      Sec 4.9    fig:uncertainty

clear; close all; clc;

%% ======================= USER SETTINGS =================================
cfg.file      = fullfile('..','calibration','Force_Rig_Calibration_Workbook.xlsx');
cfg.outDir    = 'figures';
cfg.font      = 'Courier New'; % ONE monospaced font for all graph text (use 'Courier' on macOS/Linux if missing)
cfg.fs        = 11;            % pt, ONE size for all text: ticks, labels, legends, panel titles, annotations
                               % (figures are exported at print size, so 11 pt = the paper's body text)
cfg.w1        = 12.0;          % cm, figure width for 0.75\linewidth figures
cfg.w2        = 16.0;          % cm, figure width for \linewidth figures (A4, 25 mm margins)
cfg.exportPNG = true;
cfg.dpi       = 600;
cfg.Tlimit    = 0.95;          % cyclic fidelity criterion (Eq. transmission)

% Declared resolution r (ISO 7500-1, Cl. 6.2):
%   'iso'      = larger of the display increment and half the p-p of the
%                UNLOADED reading with the drive energised (Cl. 6.2.3)
%   'workbook' = the workbook's choice: worst half p-p of all four states,
%                including the preloaded ones (conservative)
cfg.resolution = 'iso';

% Drift term of the indicated-force budget:
%   'loopOn'   = larger of zero drift after warm-up (1 h) and loop-active creep
%   'workbook' = as workbook T13 (worst of loop-on and loop-OFF creep). The
%                loop-off decay is real load relaxation, not an indication
%                error, so 'loopOn' is the correct choice for the paper.
cfg.driftSource = 'loopOn';

% Temperature contribution u_T (% of reading). Leave NaN to omit, or enter
% e.g. TC_span*dT/sqrt(3) from the load-cell datasheet and Setup!B19.
cfg.uT = NaN;

%% ======================= STYLE =========================================
C.blue   = [ 42 120 214]/255;   % series 1
C.orange = [235 104  52]/255;   % series 2
C.aqua   = [ 27 175 122]/255;   % series 3
C.yellow = [237 161   0]/255;   % series 4
C.ink    = [ 11  11  11]/255;   % primary text
C.ink2   = [ 82  81  78]/255;   % secondary text / reference lines
C.band   = [0.91 0.91 0.90];    % settling-band shading
C.grid   = [0.80 0.80 0.80];
S  = {C.blue, C.orange, C.aqua, C.yellow};
MK = {'o','s','^','d'};

% Resolve paths relative to THIS script's folder, so it works whatever
% MATLAB's current folder is.
here = fileparts(mfilename('fullpath'));
if isempty(here), here = pwd; end
if ~isfile(cfg.file) && isfile(fullfile(here, cfg.file)), cfg.file = fullfile(here, cfg.file); end
if ~startsWith(cfg.outDir, filesep) && ~contains(cfg.outDir, ':'), cfg.outDir = fullfile(here, cfg.outDir); end
if ~exist(cfg.outDir,'dir')
    [ok, msg] = mkdir(cfg.outDir);
    assert(ok, 'Cannot create the output folder %s (%s). Set cfg.outDir to a folder you can write to.', cfg.outDir, msg);
end
f = cfg.file;
assert(isfile(f), 'Workbook not found: %s (expected in ../calibration)', f);

%% ======================= SESSION CONSTANTS =============================
FN  = readScalar(f,'Setup','B22', 90);    % kg, working range F_N
DB  = readScalar(f,'Setup','B23', 0.4);   % kg, +/- settling band
Ustd= readScalar(f,'Setup','B25', NaN);   % UTM relative expanded uncertainty (fraction, k = 2)
lim = [readScalar(f,'Setup','C30',1.0), readScalar(f,'Setup','C31',1.0), ...
       readScalar(f,'Setup','C32',1.5), readScalar(f,'Setup','C33',0.1), ...
       readScalar(f,'Setup','C34',0.5)];  % class limits q b v f0 a (%)
qL  = lim(1);
k   = readScalar(f,'LC_Reference','B20', NaN);
c   = readScalar(f,'LC_Reference','B21', NaN);
R2  = readScalar(f,'LC_Reference','B22', NaN);
rsd = readScalar(f,'LC_Reference','B23', NaN);
tw  = readScalar(f,'T6_Warmup_Drift','I17', NaN);       % warm-up (min)

% ---- resolution (Table 3)
T1   = readBlock(f,'T1_Noise_Resolution','A13:M16',13);
dInc = readScalar(f,'T1_Noise_Resolution','Q13', 0.01);  % display increment
half = T1(:,7)/2;                                        % half p-p per state
switch lower(cfg.resolution)
    case 'iso',      r = max(dInc, half(2));             % unloaded, drive on
    otherwise,       r = readScalar(f,'T1_Noise_Resolution','Q15', max(half));
end

repFile = fullfile(cfg.outDir,'paper_table_values.txt');
[rep, msg] = fopen(repFile,'w');
if rep < 0
    warning('Cannot write %s (%s). Is it open in another program, or the folder read-only (e.g. OneDrive syncing)? Values go to the Command Window only.', repFile, msg);
    rep = [];
end
logf = @(varargin) fprintfBoth(rep, varargin{:});
logf('RIGOR - values for the tables of main.tex, generated %s\n', datestr(now));
logf('Workbook: %s\n', f);
logf('F_N = %g kg, settling band = +/-%g kg, k = %.6e kg/count, c = %.4f kg\n', FN, DB, k, c);
logf('Declared resolution r = %.4f kg  (cfg.resolution = ''%s'')\n', r, cfg.resolution);
logf('Drift term source: %s\n\n', cfg.driftSource);

logf('TABLE 3  tab:cal:noise   (n & mean & SD & p-p & declared r, kg)\n');
logf('  Display increment (nominal)       & --- & --- & --- & --- & %.3f \\\\\n', dInc);
nm3 = {'Idle, driver de-energised','Idle, driver energised','In contact, preloaded, driver off','In contact, preloaded, driver on'};
for i = 1:4
    logf('  %-34s & %d & %.4f & %.4f & %.4f & %s \\\\\n', nm3{i}, T1(i,2), T1(i,3), T1(i,4), T1(i,7), ...
        ternary(i==2 && strcmpi(cfg.resolution,'iso'), sprintf('%.3f', r), sprintf('(%.4f)', half(i))));
end
logf('  -> declared r = %.4f kg. Bracketed values are half p-p for information.\n', r);

%% ======================= CALIBRATION (Tables 4-7, 12; Figure 5) ========
LC   = readBlock(f,'LC_Reference','A31:I41',9);
nom  = LC(:,1);
refU = LC(:,[2 4 6]);  cntU = LC(:,[3 5 7]);
refD = LC(:,8);        cntD = LC(:,9);
indU = k*cntU + c;     indD = k*cntD + c;
resU = indU - refU;    resD = indD - refD;

fig = newFig(cfg.w1, 10.0, cfg);
ax = axes(fig); hold(ax,'on'); styleAx(ax,cfg,C);
xx = linspace(0, max(refU(:))*1.03, 200);
hL = plot(ax, xx,  qL/100*xx, '--', 'Color', C.ink2, 'LineWidth', 0.8);
plot(ax, xx, -qL/100*xx, '--', 'Color', C.ink2, 'LineWidth', 0.8, 'HandleVisibility','off');
yline(ax, 0, '-', 'Color', C.grid, 'LineWidth', 0.6, 'HandleVisibility','off');
h = gobjects(4,1);
for j = 1:3
    h(j) = plot(ax, refU(:,j), resU(:,j), ['-' MK{j}], 'Color', S{j}, ...
        'MarkerFaceColor', S{j}, 'MarkerSize', 4, 'LineWidth', 1);
end
h(4) = plot(ax, refD, resD, ['-' MK{4}], 'Color', S{4}, ...
    'MarkerFaceColor', 'w', 'MarkerSize', 4, 'LineWidth', 1);
xlabel(ax,'Reference force (kg)'); ylabel(ax,'Indicated - reference (kg)');
xlim(ax,[0 xx(end)]);
yl = max(abs([resU(:); resD]))*1.6; ylim(ax, [-1 1]*max(yl, 0.25));
lg = legend(ax, [h; hL], {'Series 1 (up)','Series 2 (up)', ...
    'Series 3 (up)','Decreasing', sprintf('\\pm%g %% limit', qL)}, ...
    'Location','southoutside','NumColumns',2,'Box','off','FontSize',cfg.fs);
spaceLegendColumns(lg);
saveFig(fig,'Fig_CalResiduals',cfg);

lvl = nom >= 10;                         % zero row excluded from q, b, v
cls = lvl & nom <= FN;                   % classified range 10 kg .. F_N
qj  = (indU - refU)./refU*100;  qm = mean(qj,2);  b = max(qj,[],2) - min(qj,[],2);
v   = (indD - mean(indU,2))./refD*100;

logf('\nTABLE 4  tab:cal:relres   (nominal kg & N & mean F_i & a %% & class limit)\n');
for n = [10 20 40 60 80 90]
    i = find(nom==n,1); Fi = mean(indU(i,:));
    logf('  %3d & %6.1f & %7.3f & %5.3f & %.1f \\\\\n', n, n*9.80665, Fi, r/Fi*100, lim(5));
end

logf('\nTABLE 5  tab:cal:main   (nominal & mean ref & ind S1 & S2 & S3 & F'' & q & b & v)\n');
for i = find(lvl)'
    logf('  %3d & %7.3f & %7.3f & %7.3f & %7.3f & %7.3f & %+6.3f & %5.3f & %+6.3f \\\\\n', ...
        nom(i), mean(refU(i,:)), indU(i,1), indU(i,2), indU(i,3), indD(i), qm(i), b(i), v(i));
end
f0_iso = k*(cntD(1) - cntU(1,1))/FN*100;     % residual after zeroing (Eq. zeroerr)
f0_abs = indD(1)/FN*100;                     % absolute zero indication (includes intercept c)
logf('  f0 (ISO, residual after zeroing)   = %.3f %%   [%.4f kg]\n', f0_iso, k*(cntD(1)-cntU(1,1)));
logf('  f0 (workbook, absolute indication) = %.3f %%   [includes the untared intercept c]\n', f0_abs);
logf('  class limits q / b / v / f0 = %.1f / %.1f / %.1f / %.2f %%\n', lim(1:4));

[mx, iMx] = max(abs(resU(lvl,:)),[],'all','linear'); %#ok<ASGLU>
logf('\nTABLE 6  tab:cal:fit\n');
logf('  Slope k & %.6e \\\\\n  Intercept c & %.4f \\\\\n  R^2 & %.6f \\\\\n', k, c, R2);
logf('  Residual SD & %.3f \\\\\n  Max residual & %.3f \\\\\n  Nonlinearity & %.3f \\\\   (%% of F_N = %g kg)\n', ...
    rsd, mx, mx/FN*100, FN);
logf('  Sensitivity & %.0f \\\\\n', 1/k);

aLow = r/mean(indU(find(nom==10,1),:))*100;
logf('\nTABLE 7  tab:cal:class   (worst over %g-%g kg)\n', 10, FN);
logf('  q   & %.2f & %.1f \\\\\n', max(abs(qm(cls))), lim(1));
logf('  b   & %.2f & %.1f \\\\\n', max(b(cls)),       lim(2));
logf('  v   & %.2f & %.1f \\\\\n', max(abs(v(cls))),  lim(3));
logf('  f0  & %.3f & %.2f \\\\   (ISO definition; workbook absolute value %.3f)\n', abs(f0_iso), lim(4), f0_abs);
logf('  a   & %.3f (at 10 kg) & %.1f \\\\\n', aLow, lim(5));
pass = [max(abs(qm(cls)))<=lim(1), max(b(cls))<=lim(2), max(abs(v(cls)))<=lim(3), abs(f0_iso)<=lim(4), aLow<=lim(5)];
logf('  criteria met for the limits in Setup!C30:C34: %s\n', mat2str(pass));
logf('  lower limit of range (a <= %.1f %%) = r / %.3f = %.1f kg\n', lim(5), lim(5)/100, r/(lim(5)/100));

logf('\nTABLE 12  tab:cal:hyst   (level & F_up & F_down & gap & h %%FS)\n');
for n = [20 40 60 80]
    i = find(nom==n,1); up = mean(indU(i,:)); dn = indD(i);
    logf('  %3d & %7.3f & %7.3f & %+7.4f & %+7.4f \\\\\n', n, up, dn, up-dn, (up-dn)/FN*100);
end

%% ======================= COMMAND ACCURACY (Table 8) ====================
T2 = readBlock(f,'T2_Command_Accuracy','A13:H212',8);
T2 = T2(~isnan(T2(:,1)),:);
nFail = sum(isnan(T2(:,3)));
T2 = T2(~isnan(T2(:,3)),:);
sp = unique(T2(:,1));
err = T2(:,3) - T2(:,1);
ts  = T2(:,4)/1000;
jit = (T2(:,2)-mean(unique(T2(:,2))))*0.5;

fig = newFig(cfg.w2, 9.2, cfg);
tl = tiledlayout(fig,1,2,'TileSpacing','compact','Padding','compact');
ax = nexttile(tl); hold(ax,'on'); styleAx(ax,cfg,C);
xr = [0 max(sp)+5];
bandPatch(ax, xr, -DB, DB, C.band);
yline(ax,0,'-','Color',C.ink2,'LineWidth',0.6,'HandleVisibility','off');
h1 = plot(ax, T2(:,1)+jit, err, 'o', 'Color', C.blue, 'MarkerSize', 3, 'LineWidth', 0.6);
mE = arrayfun(@(s) mean(err(T2(:,1)==s)), sp);
sE = arrayfun(@(s) std(err(T2(:,1)==s)), sp);
h2 = errorbar(ax, sp, mE, sE, 'o', 'Color', C.ink, 'MarkerFaceColor', C.ink, ...
    'MarkerSize', 4, 'LineWidth', 0.9, 'CapSize', 3);
xlim(ax,xr); ylim(ax,[-1 1]*DB*1.5);
xlabel(ax,'Commanded force F_c (kg)'); ylabel(ax,'F_a - F_c (kg)');
text(ax, xr(2)-1, DB*0.88, sprintf('settling band \\pm%g kg', DB), 'HorizontalAlignment','right', ...
    'FontSize', cfg.fs, 'Color', C.ink2, 'FontName', cfg.font);
lg = legend(ax,[h1 h2],{'Individual approach','Mean \pm 1 SD'},'Location','southoutside','NumColumns',2, ...
    'Box','off','FontSize',cfg.fs);
spaceLegendColumns(lg);
legendBelowLayout(lg);
panelLabel(ax,'(a)',cfg);

ax = nexttile(tl); hold(ax,'on'); styleAx(ax,cfg,C);
plot(ax, T2(:,1)+jit, ts, 'o', 'Color', C.blue, 'MarkerSize', 3, 'LineWidth', 0.6);
mT = arrayfun(@(s) mean(ts(T2(:,1)==s)), sp);
plot(ax, sp, mT, '-o', 'Color', C.ink, 'MarkerFaceColor', C.ink, 'MarkerSize', 4, 'LineWidth', 0.9);
xlim(ax,xr); ylim(ax,[0 max(ts)*1.1]);
xlabel(ax,'Commanded force F_c (kg)'); ylabel(ax,'Settling time t_s (s)');
panelLabel(ax,'(b)',cfg);
saveFig(fig,'Fig_CommandAccuracy',cfg);

logf('\nTABLE 8  tab:cal:command   (Fc & mean Fa & s & CV%% & e_c%% & e_c,FS%% & mean t_s & mean overshoot%%)\n');
for s = sp'
    m = T2(:,1)==s; a = T2(m,3); e = a - s;
    logf('  %3g & %7.3f & %.4f & %.2f & %+.2f & %+.3f & %5.1f & %4.1f \\\\   (n=%d)\n', s, mean(a), std(a), ...
        std(a)/mean(a)*100, mean(e)/s*100, mean(e)/FN*100, mean(ts(m)), mean(T2(m,5)), sum(m));
end
logf('  worst |e_c,FS| = %.3f %%FS, RMS = %.3f %%FS; median t_s = %.1f s; %d approach(es) did not settle\n', ...
    max(abs(err))/FN*100, sqrt(mean(err.^2))/FN*100, median(ts), nFail);

%% ======================= REPEATABILITY / RESEATING (Tables 9-11) =======
T3 = readBlock(f,'T3_Repeatability','A13:F212',6);   T3 = T3(~isnan(T3(:,3)),:);
T4 = readBlock(f,'T4_Seating_GageRR','A13:F212',6);  T4 = T4(~isnan(T4(:,4)),:);

fig = newFig(cfg.w2, 9.2, cfg);
tl = tiledlayout(fig,1,2,'TileSpacing','compact','Padding','compact');
ax1 = nexttile(tl); hold(ax1,'on'); styleAx(ax1,cfg,C);
sp3 = unique(T3(:,1)); bandPatch(ax1,[0.4 numel(sp3)+0.6],-DB,DB,C.band);
yline(ax1,0,'-','Color',C.ink2,'LineWidth',0.6,'HandleVisibility','off');
logf('\nTABLE 9  tab:cal:repeat   (Fc & mean & SD & CV%% & range)\n');
cv3 = nan(numel(sp3),1);
for i = 1:numel(sp3)
    m = T3(:,1)==sp3(i); d = T3(m,3)-sp3(i); n = numel(d);
    x = i + linspace(-0.18,0.18,n)';
    plot(ax1, x, d, 'o', 'Color', C.blue, 'MarkerSize', 3.5, 'LineWidth', 0.6);
    errorbar(ax1, i+0.32, mean(d), std(d), 'o', 'Color', C.ink, 'MarkerFaceColor', C.ink, ...
        'MarkerSize', 4, 'LineWidth', 0.9, 'CapSize', 3);
    a = T3(m,3); cv3(i) = std(a)/mean(a)*100;
    logf('  %3g & %7.3f & %.4f & %.3f & %.4f \\\\   (n=%d)\n', sp3(i), mean(a), std(a), cv3(i), max(a)-min(a), n);
end
set(ax1,'XTick',1:numel(sp3),'XTickLabel',cellstr(compose('%g',sp3))); xlim(ax1,[0.4 numel(sp3)+0.6]);
xlabel(ax1,'Commanded force F_c (kg)'); ylabel(ax1,'F_a - F_c (kg)');
panelLabel(ax1,'(a) Short-term, undisturbed',cfg);

ax2 = nexttile(tl); hold(ax2,'on'); styleAx(ax2,cfg,C);
sp4 = unique(T4(:,2)); ops = unique(T4(:,1));
bandPatch(ax2,[0.4 numel(sp4)+0.6],-DB,DB,C.band);
yline(ax2,0,'-','Color',C.ink2,'LineWidth',0.6,'HandleVisibility','off');
hOp = gobjects(numel(ops),1);
for o = 1:numel(ops)
    for i = 1:numel(sp4)
        m = T4(:,1)==ops(o) & T4(:,2)==sp4(i); d = T4(m,4)-sp4(i);
        x = i + (o-(numel(ops)+1)/2)*0.22 + linspace(-0.06,0.06,numel(d))';
        hOp(o) = plot(ax2, x, d, MK{o}, 'Color', S{o}, 'MarkerFaceColor', S{o}, ...
            'MarkerSize', 4, 'LineWidth', 0.6);
    end
end
set(ax2,'XTick',1:numel(sp4),'XTickLabel',cellstr(compose('%g',sp4))); xlim(ax2,[0.4 numel(sp4)+0.6]);
xlabel(ax2,'Commanded force F_c (kg)');
lg = legend(ax2,hOp,cellstr(compose('Operator %d',ops)),'Location','southoutside','NumColumns',numel(ops),'Box','off', ...
    'FontSize',cfg.fs);
spaceLegendColumns(lg);
legendBelowLayout(lg);
panelLabel(ax2,'(b) Load path reseated',cfg);
linkaxes([ax1 ax2],'y'); ylim(ax1,[-1 1]*DB*1.5);
saveFig(fig,'Fig_Repeatability',cfg);

logf('\nTABLE 10  tab:cal:seating   (Fc & mean & SD & CV%% & range)\n');
cv4 = nan(numel(sp4),1);
for i = 1:numel(sp4)
    a = T4(T4(:,2)==sp4(i),4); cv4(i) = std(a)/mean(a)*100;
    logf('  %3g & %7.3f & %.4f & %.3f & %.4f \\\\   (n=%d)\n', sp4(i), mean(a), std(a), cv4(i), max(a)-min(a), numel(a));
end

G = grrAnova(T4(:,1), T4(:,2), T4(:,4));
logf('\nTABLE 11  tab:cal:grr   (SD kg & SD %%FS)   [%d operators x %d setpoints x %d trials]\n', G.nOp, G.nPart, G.nRep);
logf('  Repeatability (EV)   & %.4f & %.3f \\\\\n', G.EV,  G.EV/FN*100);
logf('  Reproducibility (AV) & %.4f & %.3f \\\\\n', G.AV,  G.AV/FN*100);
logf('  Interaction          & %.4f & %.3f \\\\\n', G.INT, G.INT/FN*100);
logf('  Total gauge R&R      & %.4f & %.3f \\\\\n', G.GRR, G.GRR/FN*100);
logf('  (MS operator %.5f, MS interaction %.5f, MS error %.5f; negative components set to 0)\n', G.MSo, G.MSi, G.MSe);

%% ======================= DIRECTION DEPENDENCE (Table 13) ===============
T5 = readBlock(f,'T5_Direction','A13:G112',7); T5 = T5(~isnan(T5(:,3)) & ~isnan(T5(:,4)),:);
sp5 = unique(T5(:,1));
fig = newFig(cfg.w1, 9.4, cfg);
ax = axes(fig); hold(ax,'on'); styleAx(ax,cfg,C);
bandPatch(ax,[0.4 numel(sp5)+0.6],-DB,DB,C.band);
yline(ax,0,'-','Color',C.ink2,'LineWidth',0.6,'HandleVisibility','off');
logf('\nTABLE 13  tab:cal:backlash   (Fc & below & above & mean dF & worst |dF| & worst %% of band)  band width = %g kg\n', 2*DB);
worstDir = 0;
for i = 1:numel(sp5)
    m = find(T5(:,1)==sp5(i));
    for j = 1:numel(m)
        off = (j-(numel(m)+1)/2)*0.07;
        plot(ax, i+[-0.15 0.15]+off, T5(m(j),3:4)-sp5(i), '-', 'Color', C.grid, 'LineWidth', 0.6, 'HandleVisibility','off');
        hB = plot(ax, i-0.15+off, T5(m(j),3)-sp5(i), 'o', 'Color', S{1}, 'MarkerFaceColor', S{1}, 'MarkerSize', 4);
        hA = plot(ax, i+0.15+off, T5(m(j),4)-sp5(i), 's', 'Color', S{2}, 'MarkerFaceColor', S{2}, 'MarkerSize', 4);
    end
    d = T5(m,3)-T5(m,4); worstDir = max(worstDir, max(abs(d)));
    logf('  %3g & %7.3f & %7.3f & %+.3f & %.3f & %.1f \\\\\n', sp5(i), mean(T5(m,3)), ...
        mean(T5(m,4)), mean(d), max(abs(d)), max(abs(d))/(2*DB)*100);
end
set(ax,'XTick',1:numel(sp5),'XTickLabel',cellstr(compose('%g',sp5))); xlim(ax,[0.4 numel(sp5)+0.6]);
ylim(ax,[-1 1]*DB*1.5);
xlabel(ax,'Commanded force F_c (kg)'); ylabel(ax,'F_a - F_c (kg)');
lg = legend(ax,[hB hA],{'Approached from below','Approached from above (110 %)'}, ...
    'Location','southoutside','NumColumns',1,'Box','off','FontSize',cfg.fs);
spaceLegendColumns(lg);
saveFig(fig,'Fig_Direction',cfg);

%% ======================= WARM-UP AND ZERO DRIFT (Table 14) =============
T6 = readBlock(f,'T6_Warmup_Drift','A13:F1012',6); T6 = T6(~isnan(T6(:,2)),:);
tm = T6(:,1)/60;
fig = newFig(cfg.w1, 11.0, cfg);
tl = tiledlayout(fig,2,1,'TileSpacing','compact','Padding','compact');
ax1 = nexttile(tl); hold(ax1,'on'); styleAx(ax1,cfg,C);
if ~isnan(tw)
    F_w = interp1(tm, T6(:,2), tw);
    patch(ax1,[tw tm(end) tm(end) tw],[F_w-r F_w-r F_w+r F_w+r],C.band,'EdgeColor','none','HandleVisibility','off');
    xline(ax1, tw, '--', sprintf('warm-up %.1f min', tw), 'Color', C.ink2, 'LineWidth', 0.8, ...
        'FontSize', cfg.fs, 'FontName', cfg.font, 'LabelVerticalAlignment','bottom', 'LabelHorizontalAlignment','left');
end
plot(ax1, tm, T6(:,2), '-', 'Color', C.blue, 'LineWidth', 1);
ylabel(ax1,'Indication (kg)'); set(ax1,'XTickLabel',[]);
panelLabel(ax1,'(a)',cfg);
ax2 = nexttile(tl); hold(ax2,'on'); styleAx(ax2,cfg,C);
if ~isnan(tw), xline(ax2, tw, '--', 'Color', C.ink2, 'LineWidth', 0.8); end
plot(ax2, tm, T6(:,5), '-', 'Color', C.orange, 'LineWidth', 1);
ylabel(ax2,['Driver T (' char(176) 'C)']); xlabel(ax2,'Time from cold start (min)');
panelLabel(ax2,'(b)',cfg);
linkaxes([ax1 ax2],'x'); xlim(ax1,[0 tm(end)]);
saveFig(fig,'Fig_WarmupDrift',cfg);

drift6 = readScalar(f,'T6_Warmup_Drift','I20',NaN);
logf('\nTABLE 14  tab:cal:warmup\n');
logf('  Indication at t = 0          & %.4f \\\\\n', readScalar(f,'T6_Warmup_Drift','I13',NaN));
logf('  Indication at t = 60 min     & %.4f \\\\\n', readScalar(f,'T6_Warmup_Drift','I14',NaN));
logf('  Total excursion (kg)         & %.4f \\\\\n', readScalar(f,'T6_Warmup_Drift','I15',NaN));
logf('  Total excursion (%%FS)        & %.3f \\\\\n', readScalar(f,'T6_Warmup_Drift','I16',NaN));
logf('  Declared warm-up (min)       & %.1f \\\\\n', tw);
logf('  Residual drift (kg/h)        & %.4f \\\\\n', drift6);
logf('  Ambient change (C)           & %.1f \\\\   (Setup!B19)\n', readScalar(f,'Setup','B19',NaN));
logf('  Driver temperature rise (C)  & %.1f \\\\\n', readScalar(f,'T6_Warmup_Drift','I25',NaN));

%% ======================= CREEP LOOP ON / OFF (Table 15) ================
T7 = readBlock(f,'T7_Creep_LoopOn','A13:F1012',6);  T7 = T7(~isnan(T7(:,2)),:);
T8 = readBlock(f,'T8_Creep_LoopOff','A13:F1012',6); T8 = T8(~isnan(T8(:,2)),:);
fig = newFig(cfg.w1, 9.4, cfg);
ax = axes(fig); hold(ax,'on'); styleAx(ax,cfg,C);
yline(ax,0,'-','Color',C.grid,'LineWidth',0.6,'HandleVisibility','off');
xline(ax,30,':','Color',C.ink2,'LineWidth',0.8,'HandleVisibility','off');
h1 = plot(ax, T7(:,1)/60, T7(:,2)-T7(1,2), '-', 'Color', C.blue,   'LineWidth', 1);
h2 = plot(ax, T8(:,1)/60, T8(:,2)-T8(1,2), '-', 'Color', C.orange, 'LineWidth', 1);
F7 = round(median(T7(:,2))); F8 = round(median(T8(:,2)));
lg = legend(ax,[h1 h2],{sprintf('Loop on, %g kg',F7), sprintf('Loop off, %g kg',F8)}, ...
    'Location','southoutside','NumColumns',2,'Box','off','FontSize',cfg.fs);
spaceLegendColumns(lg);
xlabel(ax,'Hold time (min)'); ylabel(ax,'\DeltaF = F(t) - F(0) (kg)');
xlim(ax,[0 max([T7(end,1) T8(end,1)])/60]);
saveFig(fig,'Fig_Creep',cfg);
i7 = find(T7(:,1)<=1800,1,'last'); i8 = find(T8(:,1)<=1800,1,'last');
cr7 = T7(i7,2)-T7(1,2); cr8 = T8(i8,2)-T8(1,2);
logf('\nTABLE 15  tab:cal:creep   (condition & Fc & dF30 kg & dF30 %%FS)\n');
logf('  Loop active   & %g & %+.4f & %+.3f \\\\\n', F7, cr7, cr7/FN*100);
logf('  Loop inactive & %g & %+.4f & %+.3f \\\\\n', F8, cr8, cr8/FN*100);

%% ======================= STEP RESPONSE (Table 16) ======================
stepSheets = {'T9_Step_Response_10_20','T9_Step_Response_60_80'};
fig = newFig(cfg.w2, 12.6, cfg);
tl = tiledlayout(fig,2,numel(stepSheets),'TileSpacing','compact','Padding','compact');
logf('\nTABLE 16  tab:cal:step   (step & t_r & t_in & t_s & initial overshoot%% & max excursion%% & SS error & exits)\n');
trAll = nan(1,numel(stepSheets));
for s = 1:numel(stepSheets)
    D = readBlock(f,stepSheets{s},'A13:F1612',6); D = D(~isnan(D(:,3)),:);
    t = D(:,3)/1000; F = D(:,4); f0 = D(1,1); f1 = D(1,2);
    M = stepMetrics(t,F,f0,f1,DB); trAll(s) = M.tr;
    lbl = char('a'+s-1);
    for row = 1:2
        ax = nexttile(tl, (row-1)*numel(stepSheets)+s); hold(ax,'on'); styleAx(ax,cfg,C);
        if row==1, xr = [0 t(end)]; else, xr = [0 max(2.5, M.tFirst+1.5)]; end
        bandPatch(ax, xr, f1-DB, f1+DB, C.band);
        yline(ax, f1, '--', 'Color', C.ink2, 'LineWidth', 0.7, 'HandleVisibility','off');
        plot(ax, t, F, '-', 'Color', C.blue, 'LineWidth', 1);
        if row==1
            xline(ax, M.ts, ':', sprintf('t_s = %.1f s', M.ts), 'Color', C.orange, 'LineWidth', 1, ...
                'FontSize', cfg.fs, 'FontName', cfg.font, 'LabelVerticalAlignment','bottom');
            ylim(ax, [min(f0,f1)-0.15*abs(f1-f0), max(f0,f1)+0.15*abs(f1-f0)]);
            panelLabel(ax, sprintf('(%s) %g \\rightarrow %g kg, full record', lbl, f0, f1), cfg);
        else
            y10 = f0+0.1*(f1-f0); y90 = f0+0.9*(f1-f0);
            plot(ax, [M.t10 M.t90], [y10 y90], 'o', 'Color', C.orange, 'MarkerFaceColor', C.orange, 'MarkerSize', 4);
            plot(ax, M.tFirst, M.FFirst, 's', 'Color', C.aqua, 'MarkerFaceColor', C.aqua, 'MarkerSize', 4);
            text(ax, M.t90, y90, sprintf('  t_r = %.2f s', M.tr), 'FontSize', cfg.fs, 'FontName', cfg.font, ...
                'Color', C.ink, 'VerticalAlignment','top');
            text(ax, M.tFirst, M.FFirst, sprintf('  t_{in} = %.2f s', M.tFirst), ...
                'FontSize', cfg.fs, 'FontName', cfg.font, 'Color', C.ink, 'VerticalAlignment','bottom');
            ylim(ax, [min(f0,f1)-0.1*abs(f1-f0), max(f0,f1)+0.1*abs(f1-f0)]);
            panelLabel(ax, sprintf('(%s) first %.1f s', char('a'+numel(stepSheets)+s-1), xr(2)), cfg);
            xlabel(ax,'Time (s)');
        end
        xlim(ax, xr);
        if s==1, ylabel(ax,'Force (kg)'); end
    end
    logf('  %g -> %g & %.2f & %.2f & %.2f & %.1f & %.1f & %+.3f & %d \\\\   (record %.1f s)\n', ...
        f0, f1, M.tr, M.tFirst, M.ts, M.osInit, M.osAll, M.ess, M.exits, t(end));
end
saveFig(fig,'Fig_StepResponse',cfg);

%% ======================= CYCLIC FIDELITY (Table 17) ====================
T10 = readBlock(f,'T10_Cyclic','A13:J412',10); T10 = T10(~isnan(T10(:,5)),:);
hp  = sort(unique(T10(:,4)),'descend');
fr = 1./(2*hp); Tm = nan(size(hp)); Tlo = Tm; Thi = Tm;
fig = newFig(cfg.w2, 9.2, cfg);
tl = tiledlayout(fig,1,2,'TileSpacing','compact','Padding','compact');
ax = nexttile(tl); hold(ax,'on'); styleAx(ax,cfg,C);
logf('\nTABLE 17  tab:cal:cyclic   (period & frequency & F_peak & F_valley & T & dwell%%), cycles >= 5\n');
for i = 1:numel(hp)
    m = T10(:,4)==hp(i) & T10(:,1)>=5;
    Ti = (T10(m,5)-T10(m,6))./(T10(m,3)-T10(m,2));
    Tm(i) = mean(Ti); Tlo(i) = min(Ti); Thi(i) = max(Ti);
    dwell = (1 - mean(T10(m,7))/1000/hp(i))*100;
    logf('  %4g & %.3f & %.2f & %.2f & %.3f & %.1f \\\\   (T range %.3f-%.3f, n=%d cycles)\n', 2*hp(i), fr(i), ...
        mean(T10(m,5)), mean(T10(m,6)), Tm(i), dwell, Tlo(i), Thi(i), sum(m));
end
envOK = fr(Tm >= cfg.Tlimit);
logf('  shortest period with T >= %.2f: %g s (%.3g Hz)%s\n', cfg.Tlimit, 1/max(envOK), max(envOK), ...
    ternary(numel(envOK)==numel(fr), '  = highest tested; T > 1 means overshoot, not attenuation', ''));
set(ax,'XScale','log');
yline(ax, cfg.Tlimit, '--', sprintf('T = %.2f', cfg.Tlimit), 'Color', C.ink2, 'LineWidth', 0.8, ...
    'FontSize', cfg.fs, 'FontName', cfg.font, 'LabelHorizontalAlignment','left', 'HandleVisibility','off');
yline(ax, 1, ':', 'Color', C.ink2, 'LineWidth', 0.6, 'HandleVisibility','off');
errorbar(ax, fr, Tm, Tm-Tlo, Thi-Tm, 'o-', 'Color', C.blue, 'MarkerFaceColor', C.blue, ...
    'MarkerSize', 4, 'LineWidth', 1, 'CapSize', 3);
xlim(ax, [min(fr)/1.6 max(fr)*1.6]); set(ax,'XTick',fr,'XTickLabel',cellstr(compose('%.3g',fr)));
ylim(ax, [min(0.9, min(Tlo)-0.03), max(Thi)+0.05]);
xlabel(ax,'Cycle frequency (Hz)'); ylabel(ax,'Amplitude ratio T');
panelLabel(ax,'(a)',cfg);
ax = nexttile(tl); hold(ax,'on'); styleAx(ax,cfg,C);
nC = max(T10(:,1));
lo = T10(1,2); hi = T10(1,3);
patch(ax,[0.5 4.5 4.5 0.5],[lo-0.2*(hi-lo) lo-0.2*(hi-lo) hi+0.2*(hi-lo) hi+0.2*(hi-lo)],C.band, ...
    'EdgeColor','none','HandleVisibility','off');
yline(ax,lo,'--','Color',C.ink2,'LineWidth',0.7,'HandleVisibility','off');
yline(ax,hi,'--','Color',C.ink2,'LineWidth',0.7,'HandleVisibility','off');
hP = gobjects(numel(hp),1);
for i = 1:numel(hp)
    m = T10(:,4)==hp(i);
    hP(i) = plot(ax, T10(m,1), T10(m,5), ['-' MK{i}], 'Color', S{i}, 'MarkerFaceColor', S{i}, 'MarkerSize', 3.5, 'LineWidth', 0.8);
    plot(ax, T10(m,1), T10(m,6), ['-' MK{i}], 'Color', S{i}, 'MarkerFaceColor', 'w', 'MarkerSize', 3.5, 'LineWidth', 0.8, 'HandleVisibility','off');
end
xlim(ax,[0.5 nC+0.5]); ylim(ax,[lo-0.2*(hi-lo) hi+0.2*(hi-lo)]);
text(ax, 2.5, hi+0.13*(hi-lo), 'excluded', 'HorizontalAlignment','center', 'FontSize', cfg.fs, 'Color', C.ink2, 'FontName', cfg.font);
xlabel(ax,'Cycle number'); ylabel(ax,'Peak / valley (kg)');
lg = legend(ax,hP,cellstr(compose('Period %g s',2*hp)),'Location','southoutside','NumColumns',numel(hp),'Box','off','FontSize',cfg.fs);
spaceLegendColumns(lg);
legendBelowLayout(lg);
panelLabel(ax,'(b)',cfg);
saveFig(fig,'Fig_Cyclic',cfg);

%% ======================= CURRENT VS FORCE (Sec 4.8) ====================
T11 = readBlock(f,'T11_Current_vs_Force','A13:F3012',6); T11 = T11(~isnan(T11(:,1)) & ~isnan(T11(:,3)),:);
ct = T11(:,3) >= 2;   % contact rows, as in the workbook
pw = sort(unique(T11(:,1)));
fig = newFig(cfg.w1, 9.4, cfg);
ax = axes(fig); hold(ax,'on'); styleAx(ax,cfg,C);
hI = gobjects(numel(pw),1);
for i = 1:numel(pw)
    m = ct & T11(:,1)==pw(i);
    hI(i) = plot(ax, T11(m,3), abs(T11(m,4)), MK{min(i,4)}, 'Color', S{min(i,4)}, 'MarkerSize', 2.5, 'LineWidth', 0.5);
end
p = polyfit(T11(ct,3), abs(T11(ct,4)), 1);
xf = [0 FN]; hF = plot(ax, xf, polyval(p,xf), '-', 'Color', C.ink, 'LineWidth', 1);
xlabel(ax,'Force (kg)'); ylabel(ax,'|Motor current| (A)'); xlim(ax,[0 FN]);
lg = legend(ax,[hI; hF],[cellstr(compose('PWM %g',pw)); {'Linear fit'}], ...
    'Location','southoutside','NumColumns',2,'Box','off','FontSize',cfg.fs);
spaceLegendColumns(lg);
saveFig(fig,'Fig_CurrentForce',cfg);
free = T11(:,3) < 0.5 & T11(:,2) > 500;
logf('\nSEC 4.8 text + Fig_CurrentForce caption: free-travel current %.2f A (PWM %g); fit I = %.4f A/kg * F + %.3f A; highest |I| %.2f A\n', ...
    mean(abs(T11(free & T11(:,1)==pw(1),4))), pw(1), p(1), p(2), max(abs(T11(:,4))));

%% ======================= SAFETY (Table 18) =============================
logf('\nTABLE 18  tab:cal:safety   (from T12_Safety rows 13-23: item, provoked, detected, code, t ms, residual kg, threshold, acted, verdict)\n');
for rr = 13:23
    row = readcell(f,'Sheet','T12_Safety','Range',sprintf('A%d:L%d',rr,rr));
    row(cellfun(@(x) isa(x,'missing'), row)) = {''};
    if numel(row) < 12, row(end+1:12) = {''}; end          % readcell trims empty trailing cells
    logf('  %s\n', strjoin(cellfun(@cell2str, row([1 2 3 4 5 6 8 11 12]), 'UniformOutput', false), ' | '));
end
logf('  NOTE: item 8 (overcurrent) must hold the open-loop re-run; item 9 time is a timer error - report ---.\n');

%% ======================= UNCERTAINTY (Tables 19-20) ====================
lev  = [20 40 60];
bW   = max(b(cls));  vW = max(abs(v(cls)));
switch lower(cfg.driftSource)
    case 'loopon', dr = max(abs(cr7), abs(drift6)*1);   % zero drift over 1 h
    otherwise,     dr = max(abs(cr7), abs(cr8));
end
B1 = nan(9,3);       % rep res std hys drift fit T uc U
B1(1,:) = bW/(2*sqrt(3));
B1(2,:) = r./lev*100/(2*sqrt(3));
B1(3,:) = Ustd*100/2;                       % NaN until Setup!B25 is filled
B1(4,:) = vW/sqrt(3);
B1(5,:) = dr./lev*100/sqrt(3);
B1(6,:) = rsd./lev*100;
B1(7,:) = cfg.uT;
B1(8,:) = sqrt(sum(B1(1:7,:).^2,1,'omitnan'));
B1(9,:) = 2*B1(8,:);
B2 = nan(7,1);       % ind cmd dir band seat uc U   (at 40 kg)
B2(1) = B1(8,2);
B2(2) = max(abs(err))/lev(2)*100/sqrt(3);
B2(3) = worstDir/lev(2)*100/sqrt(3);
B2(4) = DB/lev(2)*100/sqrt(3);
B2(5) = G.GRR/lev(2)*100;
B2(6) = sqrt(sum(B2(1:5).^2,'omitnan'));  B2(7) = 2*B2(6);

n1 = {'u_{rep}','u_{res}','u_{std}','u_{hys}','u_{drift}','u_{fit}','u_{T}'};
n2 = {'u_{ind}','u_{cmd}','u_{dir}','u_{band}','u_{seat}'};
fig = newFig(cfg.w2, 7.6, cfg);
tl = tiledlayout(fig,1,2,'TileSpacing','compact','Padding','compact');
ax = nexttile(tl); hold(ax,'on'); styleAx(ax,cfg,C);
keep = find(~isnan(B1(1:7,2)));
barh(ax, 1:numel(keep), B1(keep,2), 0.6, 'FaceColor', C.blue, 'EdgeColor','none');
for i = 1:numel(keep)
    text(ax, B1(keep(i),2), i, sprintf(' %.3f', B1(keep(i),2)), 'FontSize', cfg.fs, 'FontName', cfg.font, 'Color', C.ink);
end
set(ax,'YTick',1:numel(keep),'YTickLabel',n1(keep),'YDir','reverse','YGrid','off');
xlim(ax,[0 max(B1(keep,2))*1.3]);
xlabel(ax,'u_i (% of reading)');
panelLabel(ax,sprintf('(a) Indicated, U = %.2f %%', B1(9,2)),cfg);
ax = nexttile(tl); hold(ax,'on'); styleAx(ax,cfg,C);
barh(ax, 1:5, B2(1:5), 0.6, 'FaceColor', C.blue, 'EdgeColor','none');
for i = 1:5
    text(ax, B2(i), i, sprintf(' %.3f', B2(i)), 'FontSize', cfg.fs, 'FontName', cfg.font, 'Color', C.ink);
end
set(ax,'YTick',1:5,'YTickLabel',n2,'YDir','reverse','YGrid','off');
xlim(ax,[0 max(B2(1:5))*1.3]);
xlabel(ax,'u_i (% of reading)');
panelLabel(ax,sprintf('(b) Generated, U = %.2f %%', B2(7)),cfg);
saveFig(fig,'Fig_Uncertainty',cfg);

logf('\nTABLE 19  tab:cal:uncertainty   (%% of reading at %g / %g / %g kg)\n', lev);
nm1 = {'u_rep','u_res','u_std','u_hys','u_drift','u_fit','u_T','u_c','U (k=2)'};
for i = 1:9, logf('  %-8s & %s \\\\\n', nm1{i}, strjoin(compose('%.3f',B1(i,:)),' & ')); end
logf('  U (kg)   & %s \\\\\n', strjoin(compose('%.3f',B1(9,:).*lev/100),' & '));
if isnan(Ustd), logf('  NOTE: u_std is empty until Setup!B25 (UTM uncertainty) is filled.\n'); end
logf('\nTABLE 20  tab:cal:uncertgen   (%% at %g kg)\n', lev(2));
nm2 = {'u_ind','u_cmd','u_dir','u_band','u_seat','u_c','U (k=2)'};
for i = 1:7, logf('  %-8s & %.3f \\\\\n', nm2{i}, B2(i)); end

%% ======================= SUMMARY (Table 21) ============================
logf('\nTABLE 21  tab:cal:summary\n');
logf('  Calibrated range                     & 10 -- %g \\\\\n', FN);
logf('  Assigned class                       & (your judgement from Table 7) \\\\\n');
logf('  Relative indication error, worst     & %.2f \\\\\n', max(abs(qm(cls))));
logf('  Relative repeatability, worst        & %.2f \\\\\n', max(b(cls)));
logf('  Relative reversibility, worst        & %.2f \\\\\n', max(abs(v(cls))));
logf('  Nonlinearity (%%FS)                   & %.2f \\\\\n', mx/FN*100);
logf('  Noise floor (SD, unloaded)           & %.4f \\\\\n', T1(1,4));
logf('  Command-to-achieved error, worst     & %.2f \\\\\n', max(abs(err))/FN*100);
logf('  Short-term repeatability, worst CV   & %.2f \\\\\n', max(cv3));
logf('  Reproducibility (reseating), worst CV& %.2f \\\\\n', max(cv4));
logf('  Gauge R&R, SD (kg)                   & %.3f \\\\\n', G.GRR);
logf('  Approach-direction dependence, worst & %.3f \\\\\n', worstDir);
logf('  Zero drift after warm-up (kg/h)      & %.4f \\\\\n', drift6);
logf('  Creep 30 min, loop on / off (%%FS)    & %+.2f / %+.2f \\\\\n', cr7/FN*100, cr8/FN*100);
logf('  Settling time, median (s)            & %.1f \\\\\n', median(ts));
logf('  Step rise time (s)                   & %.2f -- %.2f \\\\\n', min(trAll), max(trAll));
logf('  Practical cyclic envelope (Hz)       & >= %.3g \\\\\n', max(envOK));
logf('  U, indicated force at %g kg (%%)     & %.2f \\\\\n', lev(2), B1(9,2));
logf('  U, generated force at %g kg (%%)     & %.2f \\\\\n', lev(2), B2(7));

if ~isempty(rep), fclose(rep); end
fprintf('\nDone. Figures and paper_table_values.txt written to %s\n', cfg.outDir);

%% ======================= LOCAL FUNCTIONS ===============================
function M = readBlock(file, sheet, range, nCol)
% Numeric block read from an exact cell range; blanks/text -> NaN.
% Uses explicit import options so no header detection or column guessing
% takes place (readmatrix rejects 'NumHeaderLines' together with a block 'Range').
opts = spreadsheetImportOptions('NumVariables', nCol);
opts.Sheet     = sheet;
opts.DataRange = range;
opts = setvartype(opts, 'double');
M = readmatrix(file, opts);
if isempty(M), M = nan(0,nCol); end
if size(M,2) < nCol, M(:,end+1:nCol) = NaN; end
end

function v = readScalar(file, sheet, cell, default)
try
    v = readBlock(file, sheet, [cell ':' cell], 1);
catch
    v = [];
end
if isempty(v) || isnan(v(1)), v = default; else, v = v(1); end
end

function G = grrAnova(op, part, y)
% Crossed two-way ANOVA gauge study (operator x part, equal replicates).
ops = unique(op); parts = unique(part);
a = numel(ops); b = numel(parts); n = numel(y)/(a*b);
gm = mean(y); cellM = zeros(a,b); res = 0;
for i = 1:a
    for j = 1:b
        yy = y(op==ops(i) & part==parts(j)); cellM(i,j) = mean(yy);
        res = res + sum((yy - cellM(i,j)).^2);
    end
end
SSo = b*n*sum((mean(cellM,2)-gm).^2);
SSp = a*n*sum((mean(cellM,1)-gm).^2);
SSi = n*sum((cellM(:)-gm).^2) - SSo - SSp;
G.MSo = SSo/(a-1); G.MSi = SSi/((a-1)*(b-1)); G.MSe = res/(a*b*(n-1));
G.EV  = sqrt(G.MSe);
G.AV  = sqrt(max(0, (G.MSo - G.MSi)/(b*n)));
G.INT = sqrt(max(0, (G.MSi - G.MSe)/n));
G.GRR = sqrt(G.EV^2 + G.AV^2 + G.INT^2);
G.nOp = a; G.nPart = b; G.nRep = n;
end

function fig = newFig(wCm, hCm, cfg)
fig = figure('Units','centimeters','Position',[2 2 wCm hCm],'Color','w', ...
    'PaperUnits','centimeters','PaperSize',[wCm hCm],'Renderer','painters');
set(fig,'DefaultAxesFontName',cfg.font,'DefaultTextFontName',cfg.font, ...
    'DefaultAxesFontSize',cfg.fs,'DefaultTextFontSize',cfg.fs);
end

function styleAx(ax, cfg, C)
set(ax,'FontName',cfg.font,'FontSize',cfg.fs,'LineWidth',0.6,'TickDir','out', ...
    'TickLength',[0.012 0.012],'Box','off','XColor',C.ink2,'YColor',C.ink2, ...
    'XGrid','on','YGrid','on','GridColor',C.grid,'GridAlpha',0.6,'GridLineStyle','-', ...
    'Layer','top','LabelFontSizeMultiplier',1,'TitleFontSizeMultiplier',1);
ax.XLabel.Color = C.ink; ax.YLabel.Color = C.ink;
end

function bandPatch(ax, xr, lo, hi, col)
patch(ax,[xr(1) xr(2) xr(2) xr(1)],[lo lo hi hi],col,'EdgeColor','none','HandleVisibility','off');
end

function panelLabel(ax, str, cfg)
% Uses the axes title so tiledlayout reserves space for it (no clipping).
title(ax, str, 'FontName', cfg.font, 'FontSize', cfg.fs, 'FontWeight','bold', 'Color', [11 11 11]/255);
try, ax.TitleHorizontalAlignment = 'left'; catch, end   % R2020b+
end

function M = stepMetrics(t, F, f0, f1, db)
dF = f1 - f0; x = (F - f0)/dF;
M.t10 = t(find(x >= 0.1, 1)); M.t90 = t(find(x >= 0.9, 1)); M.tr = M.t90 - M.t10;
inb = abs(F - f1) <= db;
iIn = find(inb, 1); M.tFirst = t(iIn); M.FFirst = F(iIn);
lastOut = find(~inb, 1, 'last');
if isempty(lastOut), M.ts = t(1); elseif lastOut < numel(t), M.ts = t(lastOut+1); else, M.ts = NaN; end
if dF > 0, pk = max(F); else, pk = min(F); end
M.osAll = (pk - f1)/dF*100;                                   % largest excursion in the record
iX = find(x >= 1, 1); w = t <= t(iX) + 2;                     % within 2 s of first crossing
if dF > 0, M.osInit = (max(F(w)) - f1)/dF*100; else, M.osInit = (min(F(w)) - f1)/dF*100; end
M.ess = mean(F(max(1,end-24):end)) - f1;                      % last 0.5 s
d = diff(double(inb)); idx = (1:numel(d))';
M.exits = sum(d == -1 & idx >= iIn);                          % band exits after first entry
end

function saveFig(fig, name, cfg)
drawnow;
exportgraphics(fig, fullfile(cfg.outDir,[name '.pdf']), 'ContentType','vector', 'BackgroundColor','white');
if cfg.exportPNG
    exportgraphics(fig, fullfile(cfg.outDir,[name '.png']), 'Resolution', cfg.dpi, 'BackgroundColor','white');
end
end

function spaceLegendColumns(lg)
% MATLAB has no legend column-gap property. With more than one column, pad
% every entry to the same visible length plus a gap, using non-breaking
% spaces (ordinary trailing spaces are trimmed). With a monospaced font this
% gives evenly spaced, aligned columns.
gap = 4;                                   % extra characters between columns
if lg.NumColumns < 2, return; end
str = cellstr(lg.String);
vis = regexprep(str, '\\[a-zA-Z]+', 'x');  % \pm, \Delta ... count as one character
vis = regexprep(vis, '[_^{}]', '');        % TeX sub/superscript markup is not printed
n   = max(cellfun(@numel, vis)) + gap;
for i = 1:numel(str)
    str{i} = [str{i} repmat(char(160), 1, n - numel(vis{i}))];
end
lg.String = str;
end

function legendBelowLayout(lg)
% In a tiledlayout, put the legend below ALL panels so they stay equal in size.
try, lg.Layout.Tile = 'south'; catch, end   % R2020b+; otherwise stays below its own axes
end

function fprintfBoth(fid, varargin)
fprintf(varargin{:});
if ~isempty(fid), fprintf(fid, varargin{:}); end
end

function out = ternary(cond, a, b)
if cond, out = a; else, out = b; end
end

function s = cell2str(x)
if ischar(x) || isstring(x), s = char(x);
elseif isnumeric(x) && ~isempty(x), s = num2str(x);
else, s = '';
end
end
