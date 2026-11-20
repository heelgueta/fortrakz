package app.fortrakz.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

// Everything is drawn on a Canvas so the app needs no icon font / extra dependency.

typealias IconDraw = DrawScope.(Color) -> Unit

val iconPlay: IconDraw = { c ->
    val w = size.width; val h = size.height
    drawPath(Path().apply {
        moveTo(w * .34f, h * .2f); lineTo(w * .34f, h * .8f); lineTo(w * .82f, h * .5f); close()
    }, c)
}
val iconStop: IconDraw = { c ->
    val w = size.width; val h = size.height
    drawRoundRect(c, Offset(w * .26f, h * .26f), Size(w * .48f, h * .48f), CornerRadius(w * .06f))
}
val iconRec: IconDraw = { c -> drawCircle(c, radius = size.width * .27f) }
val iconRewind: IconDraw = { c ->
    val w = size.width; val h = size.height
    drawRect(c, Offset(w * .22f, h * .24f), Size(w * .07f, h * .52f))
    drawPath(Path().apply { moveTo(w * .78f, h * .22f); lineTo(w * .78f, h * .78f); lineTo(w * .34f, h * .5f); close() }, c)
}
val iconSliders: IconDraw = { c ->
    val w = size.width; val h = size.height
    val sw = w * .06f
    listOf(.3f to .35f, .5f to .65f, .7f to .45f).forEach { (y, k) ->
        drawLine(c, Offset(w * .2f, h * y), Offset(w * .8f, h * y), sw, StrokeCap.Round)
        drawCircle(c, w * .075f, Offset(w * (.2f + .6f * k), h * y))
    }
}
val iconFlask: IconDraw = { c ->
    val w = size.width; val h = size.height
    val p = Path().apply {
        moveTo(w * .4f, h * .2f); lineTo(w * .4f, h * .45f); lineTo(w * .22f, h * .78f)
        lineTo(w * .78f, h * .78f); lineTo(w * .6f, h * .45f); lineTo(w * .6f, h * .2f)
    }
    drawPath(p, c, style = Stroke(w * .06f, cap = StrokeCap.Round, join = androidx.compose.ui.graphics.StrokeJoin.Round))
    drawLine(c, Offset(w * .34f, h * .2f), Offset(w * .66f, h * .2f), w * .06f, StrokeCap.Round)
}
val iconShare: IconDraw = { c ->
    val w = size.width; val h = size.height
    val a = Offset(w * .72f, h * .24f); val b = Offset(w * .28f, h * .5f); val d = Offset(w * .72f, h * .76f)
    drawLine(c, a, b, w * .05f); drawLine(c, b, d, w * .05f)
    listOf(a, b, d).forEach { drawCircle(c, w * .09f, it) }
}
val iconTrash: IconDraw = { c ->
    val w = size.width; val h = size.height
    val sw = w * .06f
    drawLine(c, Offset(w * .24f, h * .3f), Offset(w * .76f, h * .3f), sw, StrokeCap.Round)
    drawLine(c, Offset(w * .42f, h * .22f), Offset(w * .58f, h * .22f), sw, StrokeCap.Round)
    drawRoundRect(c, Offset(w * .3f, h * .34f), Size(w * .4f, h * .44f), CornerRadius(w * .04f), style = Stroke(sw))
}
val iconRefresh: IconDraw = { c ->
    val w = size.width; val h = size.height
    drawArc(c, -50f, 280f, false, Offset(w * .26f, h * .26f), Size(w * .48f, h * .48f), style = Stroke(w * .06f, cap = StrokeCap.Round))
    drawPath(Path().apply { moveTo(w * .68f, h * .16f); lineTo(w * .8f, h * .36f); lineTo(w * .58f, h * .36f); close() }, c)
}
val iconBack: IconDraw = { c ->
    val w = size.width; val h = size.height
    drawLine(c, Offset(w * .62f, h * .25f), Offset(w * .36f, h * .5f), w * .07f, StrokeCap.Round)
    drawLine(c, Offset(w * .36f, h * .5f), Offset(w * .62f, h * .75f), w * .07f, StrokeCap.Round)
}

/** Round tappable button with a canvas icon. */
@Composable
fun RoundButton(
    size: Dp,
    bg: Color,
    fg: Color,
    icon: IconDraw,
    enabled: Boolean = true,
    onClick: () -> Unit,
) {
    Box(
        Modifier.size(size).alpha(if (enabled) 1f else .35f).clip(CircleShape).background(bg)
            .clickable(enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { Canvas(Modifier.fillMaxSize()) { icon(fg) } }
}

/** Small square button with an icon or letter. */
@Composable
fun SquareButton(size: Dp, bg: Color, enabled: Boolean = true, onClick: () -> Unit, content: @Composable () -> Unit) {
    Box(
        Modifier.size(size).alpha(if (enabled) 1f else .35f).clip(RoundedCornerShape(10.dp)).background(bg)
            .clickable(enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { content() }
}

@Composable
fun IconSquare(size: Dp, bg: Color, fg: Color, icon: IconDraw, enabled: Boolean = true, onClick: () -> Unit) =
    SquareButton(size, bg, enabled, onClick) { Canvas(Modifier.fillMaxSize()) { icon(fg) } }
